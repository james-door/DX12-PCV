//Windows 
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Windowsx.h>
//C++
#include <vector>
#include <algorithm>
#include <fstream>
#include <tchar.h>
#include <thread>
// Helper headers 
#include "PointCloudRenderer.h"
#include "debug.h"
//DirectXMath
#include<DirectXMath.h>


HWND createWindow(LONG clientAreaWidth, LONG clientAreaHeight, HINSTANCE hInstance, TCHAR* windowName);
std::unique_ptr<PointCloudRenderer> pcr;


void onUpdate()
{
    auto currentFrameTime = std::chrono::steady_clock::now();
    pcr->deltaTime = currentFrameTime - pcr->previousFrameTime;
    pcr->previousFrameTime = currentFrameTime;
    pcr->recaculateMVP();

}
//Message Procedure
LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!pcr) 
        return DefWindowProc(hWnd, uMsg, wParam, lParam);

        switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_PAINT:
            onUpdate();
            pcr->draw();
            break;
        case WM_SIZE:
        {
            RECT newClientArea = {};
            HANDLE_RETURN(GetClientRect(hWnd, &newClientArea) == 0);
            LONG newWidth = std::max(newClientArea.right - newClientArea.left, 1L);
            LONG newHeight = std::max(newClientArea.bottom - newClientArea.top, 1L);
            if (newWidth == pcr->rtvWidth && newHeight == pcr->rtvHeight)
                break;

            pcr->flushGPU(); //Flush GPU to avoid changing in-flight rtv
            pcr->resizeRenderTargetView(newWidth, newHeight);
            pcr->resizeViewPort(newWidth, newHeight);
            pcr->uploadNewDepthStencilBufferAndCreateView(newWidth, newHeight);

            pcr->rtvWidth = newWidth;
            pcr->rtvHeight = newHeight;
        }

        break;
        case WM_KEYDOWN:
            switch (wParam) {
            case VK_F11:
                {
                    RECT newDisplay;
                    if (pcr->fsbw)
                    {
                        SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                        HMONITOR nearestDisplay = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO display = {};
                        display.cbSize = sizeof(MONITORINFO);
                        HANDLE_RETURN(GetMonitorInfo(nearestDisplay, &display) == 0);
                        newDisplay = pcr->previousClientArea;

                    }
                    else
                    {
                        SetWindowLongPtr(hWnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW ^ (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)));
                        HANDLE_RETURN(GetWindowRect(hWnd, &pcr->previousClientArea) == 0);
                        HMONITOR nearestDisplay = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO display = {};
                        display.cbSize = sizeof(MONITORINFO);
                        HANDLE_RETURN(GetMonitorInfo(nearestDisplay, &display) == 0);
                        newDisplay = display.rcMonitor;
                    }
                    LONG displayWidth = newDisplay.right - newDisplay.left;
                    LONG displayHeight = newDisplay.bottom - newDisplay.top;

                    SetWindowPos(hWnd, HWND_NOTOPMOST, newDisplay.left, newDisplay.top, displayWidth, displayHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    pcr->fsbw = !pcr->fsbw;
                }
                break;
            
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;

            }

        case WM_MOUSEMOVE:
            {
                if (!pcr->leftMouseButtonHeld)
                    break;
                int newMousePosX = GET_X_LPARAM(lParam);
                int newMousePosY = GET_Y_LPARAM(lParam);
                if (pcr->firstMove)
                {
                    pcr->oldMousePosX = newMousePosX;
                    pcr->oldMousePosY = newMousePosY;
                    pcr->firstMove = false;
                }
                constexpr float mouseSensitivity = 1000.0f;
                pcr->yaw += (pcr->oldMousePosX - newMousePosX) * pcr->deltaTime.count() * mouseSensitivity;
                pcr->pitch += (pcr->oldMousePosY - newMousePosY) * pcr->deltaTime.count() * mouseSensitivity;
                pcr->oldMousePosX = newMousePosX;
                pcr->oldMousePosY = newMousePosY;

                pcr->yaw = std::max(-180.0f, std::min(180.0f, pcr->yaw));
                pcr->pitch = std::max(-89.0f, std::min(89.0f, pcr->pitch));
      
            }
            break;
        case WM_LBUTTONDOWN:
            pcr->leftMouseButtonHeld = true;
            break;
        case WM_LBUTTONUP: //or if mouse leaves the screen
            pcr->leftMouseButtonHeld = false;
            pcr->firstMove = true;
            pcr->oldMousePosX = 0.0f;
            pcr->oldMousePosY = 0.0f;
            break;
        case WM_MOUSEWHEEL:
            {
            constexpr float maximumFOV = 90.0f;
            constexpr float minimumFOV= 1.0f;
            constexpr float deltaFOV= 1.0f;
                short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (zDelta < 0)
                    pcr->FOV = std::min(maximumFOV, pcr->FOV + deltaFOV);
                else
                    pcr->FOV = std::max(minimumFOV, pcr->FOV - deltaFOV);
            }

            break;


        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
    return 0;
}

void processPointCloudChunckASC(const std::string &chunk, std::vector<PointCloudVertex>&verts)
{
    verts.reserve(chunk.size()/64); //approximate number of verts

    float x, y, z;
    int r, g, b;
    float nx, ny, nz;
    std::istringstream in(chunk);

    while (in >> x >> y >> z >> r >> g >> b >> nx >> ny >> nz) {
        verts.emplace_back(
            DirectX::XMFLOAT3(x, y, z),
            DirectX::XMFLOAT3(r / 255.0f, g / 255.0f, b / 255.0f)
        );
    }
}

std::unique_ptr<std::vector<PointCloudVertex>> readPointCloudASC(std::string path)
{
    std::filesystem::path objPath = path;
    if(!std::filesystem::exists(objPath))
        return nullptr;
    std::filesystem::path filePath(objPath);

    auto size = std::filesystem::file_size(objPath);
    std::ifstream in(filePath, std::ios::binary);
    unsigned int nThreads = std::thread::hardware_concurrency()/2;

    std::vector<std::vector<PointCloudVertex>> threadVertices(nThreads);

    unsigned int chunkSize = size / nThreads;
    unsigned int remainder = size % nThreads;

    std::vector<std::string>chunks(nThreads);
    int endOfLineIndex = 0;

    for (int i = 0; i < nThreads; ++i)
    {
        chunks[i].resize(chunkSize + (i == (nThreads - 1) ? remainder : 0));
        in.read(chunks[i].data(), chunkSize + (i == (nThreads - 1) ? remainder : 0));

        if (i > 0 || endOfLineIndex >= chunks[0].length())
        {
            chunks[i] = chunks[i - 1].substr(endOfLineIndex, chunks[i - 1].length() - endOfLineIndex) + chunks[i];
            chunks[i - 1] = chunks[i - 1].substr(0, endOfLineIndex);
        }

        endOfLineIndex = chunks[i].rfind('\n');
        endOfLineIndex = (endOfLineIndex == std::string::npos) ? 0 : endOfLineIndex + 1;
    }
   
    std::vector<std::thread> threads;


    auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < nThreads; ++i)
        threads.push_back(std::thread(processPointCloudChunckASC,chunks[i],std::ref(threadVertices[i])));

    for (auto& t : threads) {
        t.join();
    }

    auto combinedVerts = std::make_unique<std::vector<PointCloudVertex>>();;

    for (const auto& vec : threadVertices) {
        combinedVerts->insert(combinedVerts->end(), vec.begin(), vec.end());
    }

    auto end = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    displayErrorMessage("Point cloud loaded took: " + std::to_string(time.count())+"s.");


    return combinedVerts;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    constexpr LONG defaultClientAreaWidth = 960;
    constexpr LONG defaultClientAreaHeight = 540;
    HWND windowHandle = createWindow(defaultClientAreaWidth,defaultClientAreaHeight,hInstance,_T("Point Cloud Viewer"));

    //Try Load data to SysRam
    std::shared_ptr<std::vector<PointCloudVertex>> vertices = readPointCloudASC(lpCmdLine);
    if (!vertices) 
    {
        displayErrorMessage("Failed to load data from " + std::string(lpCmdLine) + ".");
        return 1;
    }
    UINT64 bufferSize = sizeof(PointCloudVertex) * vertices->size();

    //Try create Renderer
    try 
    {
        pcr = std::make_unique<PointCloudRenderer>(windowHandle, defaultClientAreaWidth, defaultClientAreaHeight, TRUE, vertices);
    }
    catch (const std::exception& e)
    {
        displayErrorMessage(e.what());
        return 1;
    }
    
    vertices.reset();

    ShowWindow(windowHandle, SW_SHOW);

    //Message Loop
    MSG windowMsg= {};
    while (windowMsg.message != WM_QUIT) 
    {
        GetMessage(&windowMsg, NULL, 0, 0);
        TranslateMessage(&windowMsg);
        DispatchMessage(&windowMsg);
    }

	return 0;
}

HWND createWindow(LONG clientAreaWidth, LONG clientAreaHeight,HINSTANCE hInstance, TCHAR* windowName)
{
    //Win32
   //Register a window class
    WNDCLASSEX winClass = {};
    winClass.cbSize = sizeof(WNDCLASSEX);
    winClass.style = CS_HREDRAW | CS_VREDRAW;
    winClass.lpfnWndProc = WndProc;
    winClass.cbClsExtra = 0;
    winClass.cbWndExtra = 0;
    winClass.hInstance = hInstance;
    winClass.hIcon = NULL;
    winClass.hCursor = LoadCursor(hInstance, NULL);
    winClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    winClass.lpszMenuName = NULL;
    winClass.lpszClassName = _T("window");
    winClass.hIconSm = LoadIcon(hInstance, NULL);

    HANDLE_RETURN(RegisterClassEx(&winClass) == 0);


    //Fill windowArea RECT with Client area
    RECT windowArea = {};
    windowArea.left = 0;
    windowArea.top = 0;
    windowArea.right = clientAreaWidth;
    windowArea.bottom = clientAreaHeight;

    //Find window area from the client 
    HANDLE_RETURN(AdjustWindowRect(&windowArea, WS_OVERLAPPEDWINDOW, FALSE) == 0);
    int windowAreaWidth = static_cast<int>(windowArea.right - windowArea.left);
    int windowAreaHeight = static_cast<int>(windowArea.bottom - windowArea.top);
    //Find centre of screen coordinates
    int displayWidth = GetSystemMetrics(SM_CXSCREEN);
    int displayHeight = GetSystemMetrics(SM_CYSCREEN);
    int centreX = (displayWidth - windowAreaWidth) / 2;
    int centreY = (displayHeight - windowAreaHeight) / 2;

    //Create the window
    HWND windowHandle = CreateWindowEx(NULL, _T("window"), windowName, WS_OVERLAPPEDWINDOW, centreX, centreY,
        windowAreaWidth, windowAreaHeight, NULL, NULL, hInstance, nullptr);
    HANDLE_RETURN(windowHandle == NULL);
    return windowHandle;
}