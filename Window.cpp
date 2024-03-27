//Windows 
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Windowsx.h>
//C++
#include <vector>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <tchar.h>
#include <wrl.h>
#include <optional>
#include <thread>
// Helper headers 
#include "d3dx12.h" 
#include "PointCloudRenderer.h"
#include "debug.h"
//DirectX 12
#include <DirectXColors.h>
//Direct3D
#include <d3d12.h>
#include <DXGI1_6.h>
//#include <d3dcompiler.h>
//DXGI
#include <dxgidebug.h>
//DXC
#include <dxcapi.h>
//DirectXMath
#include<DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// DEBUG
//#define GPU_BASED_VALIDATION


#if defined(DEBUG)
UINT factoryDebug = DXGI_CREATE_FACTORY_DEBUG;
ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
ComPtr<ID3D12InfoQueue> directDebugQueue;
#else
UINT factoryDebug = 0;
#endif //DEBUG



//Globals
LONG clientAreaWidth;
LONG clientAreaHeight;
ComPtr<ID3D12CommandAllocator> allocators[2];

bool d3dInit = false;
bool screenTearingEnabled = true;
bool fsbw= false;
RECT previousClientArea;
ComPtr<ID3D12GraphicsCommandList> cmdList;
ComPtr<ID3D12Resource> backBufferResources[2];
UINT rtvHeapOffset;
ComPtr<ID3D12DescriptorHeap> backBufferDescriptorHeap;
ComPtr<ID3D12CommandQueue> cmdQueue;
ComPtr<IDXGISwapChain3> swapChain;

ComPtr<ID3D12Fence> swapChainFence;
HANDLE swapChainPresentedEvent;
UINT64 swapChainFenceValues[2] = {};
UINT64 swapChainFenceValue = 0;
int activeBuffer = 0;

ComPtr<ID3D12Device2> device;

//Triangle Globals
ComPtr<ID3D12PipelineState> PSO;
ComPtr<ID3D12RootSignature> rootSignature;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
D3D12_VIEWPORT viewportDescription = {};
D3D12_RECT scissorRec = {};
int nVerts;
XMMATRIX MVP;

//Cube globals
ComPtr<ID3D12DescriptorHeap> dsvHeap;

//Camera globals
float FOV = 45.0f;
float yaw= 0.0f;
float pitch= 0.0f;
int oldMousePosX = 0.0f;
int oldMousePosY = 0.0f;
bool leftMouseButtonHeld = false;
bool firstMove = true;
XMMATRIX projectionMatrix;
XMMATRIX viewMatrix;
XMMATRIX modelMatrix;
std::chrono::steady_clock::time_point previousFrameTime;
std::chrono::duration<float, std::ratio<1, 1>> deltaTime;

//Temporary
struct ViewingSphere
{
    XMFLOAT3 centroid;
    float radius;
};
ViewingSphere viewingSphere;
void uploadDepthStencilBufferAndCreateView();
ComPtr<ID3D12Resource> dsvResource;

void onUpdate()
{
    auto currentFrameTime = std::chrono::steady_clock::now();
    deltaTime = currentFrameTime - previousFrameTime;
    previousFrameTime = currentFrameTime;
    

    //Update Model matrix
    modelMatrix = XMMatrixIdentity();

    //Update View matrix
    float radius = viewingSphere.radius * 2.0f;
    float theta = XMConvertToRadians(pitch + 90);
    float phi = XMConvertToRadians(yaw); // Adjusted to ensure phi is calculated correctly

    // Spherical to Cartesian conversion for camera position
    float x = radius * XMScalarSin(theta) * XMScalarCos(phi);
    float z = radius * XMScalarSin(theta) * XMScalarSin(phi);
    float y = radius * XMScalarCos(theta);
    XMVECTOR cameraPos = XMVectorSet(viewingSphere.centroid.x + x, viewingSphere.centroid.y + y, viewingSphere.centroid.z + z, 1.0f);
    XMVECTOR target = XMVectorSet(viewingSphere.centroid.x, viewingSphere.centroid.y, viewingSphere.centroid.z, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    viewMatrix = XMMatrixLookAtLH(cameraPos, target, up);

    //Update Projection matrix
    float aspectRatio = static_cast<float>(clientAreaWidth) / static_cast<float>(clientAreaHeight);
    projectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(FOV), aspectRatio, 0.1, 100);

}
//Message Procedure
LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (d3dInit) {
        switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_PAINT:
            onUpdate();
            ++swapChainFenceValue;
            allocators[activeBuffer]->Reset();
            cmdList->Reset(allocators[activeBuffer].Get(), NULL);

            cmdList->SetPipelineState(PSO.Get());
            cmdList->SetGraphicsRootSignature(rootSignature.Get());

            //set rasteriser state
            cmdList->RSSetViewports(1, &viewportDescription);
            cmdList->RSSetScissorRects(1, &scissorRec);
          

            //Clear the target
            //By default back buffer resources created in the swap chain are in the state D3D12_RESOURCE_STATE_PRESENT
            //To clear them they must be in the state D3D12_RESOURCE_STATE_RENDER_TARGET 
            {
                //set output-merger state
                auto activeBackBufferDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(backBufferDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                    activeBuffer, rtvHeapOffset); 
                auto dsvDescriptorHeapHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart());

                cmdList->OMSetRenderTargets(1, &activeBackBufferDescriptorHandle, FALSE, &dsvDescriptorHeapHandle);


                CD3DX12_RESOURCE_BARRIER present2RTV = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

                CD3DX12_RESOURCE_BARRIER RTV2Present = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

                
                cmdList->ResourceBarrier(1, &present2RTV);

            
                cmdList->ClearRenderTargetView(activeBackBufferDescriptorHandle, Colors::Black, 0, NULL); //clear rtv        
                cmdList->ClearDepthStencilView(dsvDescriptorHeapHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);



                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);

                //Update MVP every frame

                MVP = XMMatrixMultiply(XMMatrixMultiply(modelMatrix, viewMatrix), projectionMatrix);

                cmdList->SetGraphicsRoot32BitConstants(0, sizeof(MVP) / 4, &MVP, 0);


                cmdList->DrawInstanced(nVerts,1,0,0);

                cmdList->ResourceBarrier(1, &RTV2Present);
            }
            cmdList->Close();
            {
                ID3D12CommandList* cmdLists[] = { cmdList.Get() };
                cmdQueue->ExecuteCommandLists(1, cmdLists);
                swapChain->Present((screenTearingEnabled) ? 0 : 1,
                    (screenTearingEnabled) ? DXGI_PRESENT_ALLOW_TEARING : 0);
            }

            swapChainFenceValues[activeBuffer] = swapChainFenceValue;
            cmdQueue->Signal(swapChainFence.Get(), swapChainFenceValues[activeBuffer]);

            activeBuffer = swapChain->GetCurrentBackBufferIndex();
            swapChainFence->SetEventOnCompletion(swapChainFenceValues[activeBuffer], swapChainPresentedEvent);
            WaitForSingleObject(swapChainPresentedEvent, INFINITE);

            break;
        case WM_SIZE:

        {
            RECT newClientArea = {};
            HANDLE_RETURN(GetClientRect(hWnd, &newClientArea) == 0);
            LONG newWidth = std::max(newClientArea.right - newClientArea.left, 1L);
            LONG newHeight = std::max(newClientArea.bottom - newClientArea.top, 1L);
            if (newWidth == clientAreaWidth && newHeight == clientAreaHeight)
                break;


            //Flush GPU to avoid changing in-flight buffer
            ComPtr<ID3D12Fence> flush;
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&flush));
            cmdQueue->Signal(flush.Get(), 1);
            HANDLE flushEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            flush->SetEventOnCompletion(1, flushEvent);
            WaitForSingleObject(flushEvent, INFINITE);

            for (int i = 0; i < 2; ++i) {
                backBufferResources[i].Reset();
                swapChainFenceValues[i] = swapChainFenceValue;
            }
            //Resize back buffers
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
            swapChain->GetDesc1(&swapChainDesc);
            swapChain->ResizeBuffers(2, newWidth, newHeight, swapChainDesc.Format, swapChainDesc.Flags);
            activeBuffer = swapChain->GetCurrentBackBufferIndex();
            auto heapHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(backBufferDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
            for (int i = 0; i < 2; ++i) {
                heapHandle.Offset(i * rtvHeapOffset);
                swapChain->GetBuffer(i, IID_PPV_ARGS(&backBufferResources[i]));
                device->CreateRenderTargetView(backBufferResources[i].Get(), NULL, heapHandle);
            }
            //Resize viewport
            viewportDescription.Width = newWidth;
            viewportDescription.Height = newHeight;

            clientAreaWidth = newWidth;
            clientAreaHeight = newHeight;
            uploadDepthStencilBufferAndCreateView();
        }

        break;
        case WM_KEYDOWN:
            switch (wParam) {
            case VK_F11:
                {
                    RECT newDisplay;
                    if (fsbw)
                    {
                        SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                        HMONITOR nearestDisplay = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO display = {};
                        display.cbSize = sizeof(MONITORINFO);
                        HANDLE_RETURN(GetMonitorInfo(nearestDisplay, &display) == 0);
                        newDisplay = previousClientArea;

                    }
                    else
                    {
                        SetWindowLongPtr(hWnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW ^ (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)));
                        HANDLE_RETURN(GetWindowRect(hWnd, &previousClientArea) == 0);
                        HMONITOR nearestDisplay = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO display = {};
                        display.cbSize = sizeof(MONITORINFO);
                        HANDLE_RETURN(GetMonitorInfo(nearestDisplay, &display) == 0);
                        newDisplay = display.rcMonitor;
                    }
                    LONG displayWidth = newDisplay.right - newDisplay.left;
                    LONG displayHeight = newDisplay.bottom - newDisplay.top;

                    SetWindowPos(hWnd, HWND_NOTOPMOST, newDisplay.left, newDisplay.top, displayWidth, displayHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    fsbw = !fsbw;
                }
                break;
            
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;

            }

        case WM_MOUSEMOVE:
            {
                if (!leftMouseButtonHeld)
                    break;
                int newMousePosX = GET_X_LPARAM(lParam);
                int newMousePosY = GET_Y_LPARAM(lParam);
                if (firstMove)
                {
                    oldMousePosX = newMousePosX;
                    oldMousePosY = newMousePosY;
                    firstMove = false;
                }
                constexpr float mouseSensitivity = 1000.0f;
                yaw += (oldMousePosX - newMousePosX) * deltaTime.count() * mouseSensitivity;
                pitch += (oldMousePosY - newMousePosY) * deltaTime.count() * mouseSensitivity;
                oldMousePosX = newMousePosX;
                oldMousePosY = newMousePosY;

                yaw = std::max(-180.0f, std::min(180.0f, yaw));
                pitch = std::max(-89.0f, std::min(89.0f, pitch));

                 
            }
            break;
        case WM_LBUTTONDOWN:
            leftMouseButtonHeld = true;
            break;
        case WM_LBUTTONUP: //or if mouse leaves the screen
            leftMouseButtonHeld = false;
            firstMove = true;
            oldMousePosX = 0.0f;
            oldMousePosY = 0.0f;
            break;
        case WM_MOUSEWHEEL:
            {
            constexpr float maximumFOV = 90.0f;
            constexpr float minimumFOV= 1.0f;
            constexpr float deltaFOV= 1.0f;
                short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (zDelta < 0)
                    FOV = std::min(maximumFOV, FOV + deltaFOV);
                else
                    FOV = std::max(minimumFOV, FOV - deltaFOV);
            }

            break;


        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
    }
    else 
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    return 0;
}


//ComPtr<IDxcBlob> dxcRuntimeCompile(std::wstring shaderPath, std::vector<LPCWSTR> arguments) {
//    //Compile Shaders
//    ComPtr<IDxcUtils> dxcUtil;
//    ComPtr<IDxcBlobEncoding> uncompiledBlob;
//    HANDLE_RETURN(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtil)));
//    UINT32 shaderPage = CP_UTF8;
//    HANDLE_RETURN(dxcUtil->LoadFile(shaderPath.c_str(), &shaderPage, &uncompiledBlob));
//    DxcBuffer uncompiledBuffer = { uncompiledBlob->GetBufferPointer(),uncompiledBlob->GetBufferSize(),shaderPage };
//
//    ComPtr<IDxcCompiler3> dxcCompiler;
//    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
//
//    ComPtr<IDxcResult> result;
//    dxcCompiler->Compile(&uncompiledBuffer, arguments.data(), (UINT32)arguments.size(), nullptr, IID_PPV_ARGS(&result));
//
//    ComPtr<IDxcBlob> compiledBlob;
//    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiledBlob), nullptr);
//
//    ComPtr<IDxcBlobUtf8> pErrors;
//    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
//    if (pErrors && pErrors->GetStringLength() > 0)
//    {
//        HLSLErrorLog << (char*)pErrors->GetBufferPointer();
//    }
//    return compiledBlob;
//}
//


struct vertex {
    XMFLOAT3 modelPos;
    XMFLOAT3 colour;
    vertex(const XMFLOAT3& pos, const XMFLOAT3& col)
        : modelPos(pos), colour(col) {}
};

//ToDo: Estimate the number of points based on the file size, instead of using push_back
//ToDo: Verts should be probabbly be returned by pointer rather than value, maybe using dynamic allocation and returned a uniquie_ptr

void processPointCloudChunckASC(const std::string &chunk, std::vector<vertex>&verts)
{
    verts.reserve(chunk.size()/64); //approximate number of verts

    float x, y, z;
    int r, g, b;
    float nx, ny, nz;
    std::istringstream in(chunk);

    while (in >> x >> y >> z >> r >> g >> b >> nx >> ny >> nz) {
        verts.emplace_back(
            XMFLOAT3(x, y, z),
            XMFLOAT3(r / 255.0f, g / 255.0f, b / 255.0f)
        );
    }
}

std::optional<std::vector<vertex>> readPointCloudASC(std::string path)
{
    std::filesystem::path objPath = path;
    if(!std::filesystem::exists(objPath))
        return std::nullopt;
    std::filesystem::path filePath(objPath);

    auto size = std::filesystem::file_size(objPath);
    std::ifstream in(filePath, std::ios::binary);
    unsigned int nThreads = std::thread::hardware_concurrency()/2;

    std::vector<std::vector<vertex>> threadVertices(nThreads);

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

    std::vector<vertex> combinedVerts;

    for (const auto& vec : threadVertices) {
        combinedVerts.insert(combinedVerts.end(), vec.begin(), vec.end());
    }

    auto end = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    displayErrorMessage("Took: " + std::to_string(time.count())+"s");


    return combinedVerts;
}


ViewingSphere CalculateMinimumBoundingSphere(const std::vector<vertex>& vertices) {
    //Caclulate centroid
    XMVECTOR sumPos = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

    for (const auto& vert : vertices) {
        XMVECTOR pos = XMLoadFloat3(&vert.modelPos);
        sumPos = XMVectorAdd(sumPos, pos);
    }

    sumPos = XMVectorScale(sumPos, 1.0f / vertices.size());

    XMFLOAT3 centroid;
    XMStoreFloat3(&centroid, sumPos);
    XMVECTOR centroidVec = XMLoadFloat3(&centroid);

    //Calcualte radius 
    float maxDistance = 0.0f;
    for (const auto& vert : vertices) {
        XMVECTOR pos = XMLoadFloat3(&vert.modelPos);
        XMVECTOR distance = XMVector3Length(XMVectorSubtract(pos, centroidVec));
        float distanceValue;
        XMStoreFloat(&distanceValue,distance);
        maxDistance = std::max(maxDistance, distanceValue);

    }


    return { centroid,maxDistance };
}

std::optional<std::vector<std::byte>> loadByteCode(std::filesystem::path path)
{
    if (!std::filesystem::exists(path))
        std::nullopt;
    auto dataSize = std::filesystem::file_size(path);
    std::vector<std::byte> buffer;
    buffer.resize(dataSize);
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(buffer.data()), dataSize);
    if (!in.good() && !in.eof())
        return std::nullopt;
    return buffer;

}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{

    std::optional<std::vector<vertex>> vertices = readPointCloudASC("cube.asc");
    if (!vertices.has_value())
        return 1;


    UINT64 bufferSize = sizeof(vertex) * vertices->size();
    nVerts = vertices->size();


    viewingSphere = CalculateMinimumBoundingSphere(vertices.value());
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
    winClass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    winClass.lpszMenuName = NULL;
    winClass.lpszClassName = _T("window");
    winClass.hIconSm = LoadIcon(hInstance, NULL);

    HANDLE_RETURN(RegisterClassEx(&winClass) == 0);
   

    //Fill windowArea RECT with Client area
    constexpr LONG defaultClientAreaWidth = 960;
    constexpr LONG defaultClientAreaHeight = 540;

    clientAreaWidth = defaultClientAreaWidth;
    clientAreaHeight = defaultClientAreaHeight;

    RECT windowArea = {};
    windowArea.left = 0;
    windowArea.top = 0;
    windowArea.right = defaultClientAreaWidth;
    windowArea.bottom = defaultClientAreaHeight;

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
    HWND windowHandle = CreateWindowEx(NULL, _T("window"), _T("My Window"), WS_OVERLAPPEDWINDOW, centreX, centreY,
        windowAreaWidth, windowAreaHeight, NULL, NULL, hInstance, nullptr);
    HANDLE_RETURN(windowHandle == NULL);


    //DirectX12
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

    //Create the D3D12 Debug layer
#if defined(DEBUG)
    ComPtr<ID3D12Debug> debugController0;
    HANDLE_RETURN(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController0)));
    debugController0->EnableDebugLayer();
#if defined(GPU_BASED_VALIDATION)

    ComPtr<ID3D12Debug1> debugController1;
    debugController0.As(&debugController1);
    debugController1->SetEnableGPUBasedValidation(TRUE);

#endif //GPU_BASED_VALIDATION
    //DXGI info queue
    if (SUCCEEDED(DXGIGetDebugInterface1(NULL, IID_PPV_ARGS(&dxgiInfoQueue)))) {
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_DX, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);

        //Create filter
        DXGI_INFO_QUEUE_FILTER_DESC dxgiFilterDesc = {};
        DXGI_INFO_QUEUE_MESSAGE_SEVERITY dxgiAllowedSeverity[] = { DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING };
        dxgiFilterDesc.NumSeverities = 3;
        dxgiFilterDesc.pSeverityList = dxgiAllowedSeverity;
        DXGI_INFO_QUEUE_FILTER dxgiFilter = {};
        dxgiFilter.AllowList = { dxgiFilterDesc };
        dxgiInfoQueue->PushStorageFilter(DXGI_DEBUG_DX, &dxgiFilter);
    }

#endif //DEBUG

    //Choose adapter
    ComPtr<IDXGIFactory4> factory;
    HANDLE_RETURN(CreateDXGIFactory2(factoryDebug, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIAdapter1> bestAdapter;
    UINT n = 0;
    UINT highestVRam = 0;
    //Choose an adapter
    while (factory->EnumAdapters1(n, &adapter) == S_OK)
    {
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        HANDLE_RETURN(adapter->GetDesc1(&adapterDesc));
        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
            && SUCCEEDED(D3D12CreateDevice(adapter.Get(), featureLevel, __uuidof(ID3D12Device2), nullptr)) //Check adaptor feature level is high enough
            && adapterDesc.DedicatedVideoMemory > highestVRam)
        {
            bestAdapter = adapter;
            highestVRam = adapterDesc.DedicatedVideoMemory;
        }
        ++n;
    }

    //Create DirectX12 Device
    HANDLE_RETURN(D3D12CreateDevice(bestAdapter.Get(), featureLevel, IID_PPV_ARGS(&device)));

    //Enable Debugging Queue for DirectX 12
#if defined(DEBUG)
    if (SUCCEEDED(device.As(&directDebugQueue)))
    {
        directDebugQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        //create filter
        D3D12_INFO_QUEUE_FILTER_DESC filterDesc= {};
        D3D12_MESSAGE_SEVERITY allowedMessages[] = {D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR, D3D12_MESSAGE_SEVERITY_WARNING};
        filterDesc.NumSeverities = 3;
        filterDesc.pSeverityList = allowedMessages;
        D3D12_INFO_QUEUE_FILTER filter = { filterDesc };
        directDebugQueue->PushStorageFilter(&filter);
    }
#endif
    //Heap sizes
    rtvHeapOffset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); //Determine vendor specific RTV offset






    //Create the command queue
    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
    cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    HANDLE_RETURN(device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue)));


    //Create the swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = defaultClientAreaWidth;
    swapChainDesc.Height = defaultClientAreaHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1,0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = (screenTearingEnabled) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : NULL;

    ComPtr<IDXGISwapChain1> swapChain1;

    HANDLE_RETURN(factory->CreateSwapChainForHwnd(cmdQueue.Get(), windowHandle, &swapChainDesc, nullptr, nullptr, swapChain1.GetAddressOf()));
    swapChain1.As(&swapChain);
    activeBuffer = swapChain->GetCurrentBackBufferIndex();


    //Get references for the back buffer resources created by the swap chain
    //and also create RTV descriptors for these created resources.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    HANDLE_RETURN(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&backBufferDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE heapHandle(backBufferDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


    for (int i = 0; i < 2; ++i) {
        heapHandle.Offset(rtvHeapOffset* i);

        HANDLE_RETURN(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBufferResources[i])));
        device->CreateRenderTargetView(backBufferResources[i].Get(), NULL, heapHandle);
    }


    //Create command list
    
    for (int i = 0; i < 2; ++i) {
        HANDLE_RETURN(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
    }

    HANDLE_RETURN(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(), nullptr, IID_PPV_ARGS(&cmdList)));

    cmdList->Close();
   
    //Create fence and win32 event for synchronisation
    HANDLE_RETURN(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&swapChainFence)));
    swapChainPresentedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

   

 


    D3D12_SUBRESOURCE_DATA vertexSubResourceData = {};
    vertexSubResourceData.pData = vertices->data();
    vertexSubResourceData.RowPitch = bufferSize;
    vertexSubResourceData.SlicePitch = vertexSubResourceData.RowPitch;



    //Transfer data to VRAM
    ComPtr<ID3D12Resource> vertexResource;
    ComPtr<ID3D12Resource> vertexStagingBufferResource;
    D3D12_RESOURCE_DESC vertexResourceDesc= CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    
    HANDLE_RETURN(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &vertexResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexResource)));

    HANDLE_RETURN(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexStagingBufferResource)));

 

    cmdList->Reset(allocators[activeBuffer].Get(), nullptr);
    UpdateSubresources(cmdList.Get(), vertexResource.Get(), vertexStagingBufferResource.Get(), 0, 0, 1, &vertexSubResourceData);
    cmdList->Close();
    
    
    //Execute Command queue
    ID3D12CommandList* lists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(1, lists);

    //Flush GPU 
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
    cmdQueue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    


    //Create vertex buffer view
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = bufferSize;
    vertexBufferView.StrideInBytes = sizeof(vertex);



    //Create depth-stencil view
    //Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HANDLE_RETURN(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));

    //Create heap and resource for depth-stencil buffer
    uploadDepthStencilBufferAndCreateView();


 


    //Compile HLSL into DXIL
    
    //ComPtr<IDxcBlob> vertexShaderBlob = dxcRuntimeCompile(L"shaders/vertexShader.hlsl", { L"-T vs_6_0" });
    //ComPtr<IDxcBlob> pixelShaderBlob = dxcRuntimeCompile(L"shaders/fragmentShader.hlsl", { L"-T ps_6_0" });


  



    std::optional<std::vector<std::byte>>  vertexShaderByteCode = loadByteCode("vertexShader.dxil");
    std::optional<std::vector<std::byte>>  pixelShaderByteCode = loadByteCode("fragmentShader.dxil");
    if (!vertexShaderByteCode.has_value() || !pixelShaderByteCode.has_value())
        return 1;
    


    

    //Setup for Rasteriser state
    
    //Viewport
    viewportDescription.TopLeftX = 0;
    viewportDescription.TopLeftY = 0;
    viewportDescription.Width= clientAreaWidth;
    viewportDescription.Height= clientAreaHeight;
    viewportDescription.MinDepth = D3D12_MIN_DEPTH;
    viewportDescription.MaxDepth= D3D12_MAX_DEPTH;

    //Scissor Rectangle
    scissorRec.top = 0;
    scissorRec.left= 0;
    scissorRec.bottom = LONG_MAX;
    scissorRec.right= LONG_MAX;



    //PSO

    //Input layout
    
    D3D12_INPUT_ELEMENT_DESC vertexColourPositionLayoutDescription[2] =
    {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };

    D3D12_INPUT_LAYOUT_DESC layoutDescription = {vertexColourPositionLayoutDescription,2 };

    //Render Target View format
    D3D12_RT_FORMAT_ARRAY rtvFormat;
    rtvFormat.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::fill(&rtvFormat.RTFormats[1], &rtvFormat.RTFormats[8], DXGI_FORMAT_UNKNOWN);
    rtvFormat.NumRenderTargets = 1;

  


    //Create Root signature using run-time seralisaiton
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = (D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | 
                                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | 
                                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
                                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
                                                     );

    CD3DX12_ROOT_PARAMETER1  rootSignatureParameters[1];
    rootSignatureParameters[0].InitAsConstants(sizeof(MVP) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(1, rootSignatureParameters, 0, 0, rootSignatureFlags);



    ComPtr<ID3DBlob> seralisedRootSignature;
    ComPtr<ID3DBlob> seralisedRootSignatureErrors;

    //Check shader model feature support
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureFeatureLevel = {};
    rootSignatureFeatureLevel.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureFeatureLevel, sizeof(rootSignatureFeatureLevel))))
    {
        rootSignatureFeatureLevel.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; //if for some reason the feature check fails, set it to version 1.1
    }



    D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, rootSignatureFeatureLevel.HighestVersion, &seralisedRootSignature, &seralisedRootSignatureErrors);


    device->CreateRootSignature(0, seralisedRootSignature->GetBufferPointer(), seralisedRootSignature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

    //Create PSO
    struct StateStream {
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitive;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS targetFormat;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsFormat;
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSig;
    };

    StateStream stream;
    stream.vs = CD3DX12_PIPELINE_STATE_STREAM_VS(CD3DX12_SHADER_BYTECODE(vertexShaderByteCode->data(),vertexShaderByteCode->size()));
    stream.ps = CD3DX12_PIPELINE_STATE_STREAM_PS(CD3DX12_SHADER_BYTECODE(pixelShaderByteCode->data(),pixelShaderByteCode->size()));
    stream.inputLayout = CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT(layoutDescription);
    stream.primitive = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    stream.targetFormat = rtvFormat;
    stream.rootSig = rootSignature.Get();
    stream.dsFormat = DXGI_FORMAT_D32_FLOAT;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStream{};
    pipelineStateStream.pPipelineStateSubobjectStream = &stream;
    pipelineStateStream.SizeInBytes = sizeof(StateStream);
    HANDLE_RETURN(device->CreatePipelineState(&pipelineStateStream, IID_PPV_ARGS(&PSO)));


  
    previousFrameTime = std::chrono::steady_clock::now();

    d3dInit = true;
  




    ShowWindow(windowHandle, SW_SHOW);





    //Message Loop
    MSG windowMsg= {};
    while (windowMsg.message != WM_QUIT) {
        GetMessage(&windowMsg, NULL, 0, 0);
        TranslateMessage(&windowMsg);
        DispatchMessage(&windowMsg);
    }

//#if defined(DEBUG)
//    //DXGI
//    {
//        UINT64 numMsg = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_DX);
//        for (UINT64 i = 0; i < numMsg; ++i) {
//            SIZE_T messageSize;
//            dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, nullptr, &messageSize);
//            DXGI_INFO_QUEUE_MESSAGE* msg = (DXGI_INFO_QUEUE_MESSAGE*)malloc(messageSize);
//            dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, msg, &messageSize);
//            if (msg)
//                displayErrorMessage("DXGI Error:\n\n" + std::string(msg->pDescription));
//        }
//    }
//
//    //D3D12
//    {
//        UINT64 numMsg = directDebugQueue->GetNumStoredMessages();
//        for (UINT64 i = 0; i < numMsg; ++i) {
//            SIZE_T messageSize;
//            directDebugQueue->GetMessage(i, nullptr, &messageSize);
//            D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(messageSize);
//            directDebugQueue->GetMessage(i, msg, &messageSize);
//            if (msg)
//                displayErrorMessage("D3D Errors:\n\n" + std::string(msg->pDescription));
//        }
//    }
//#endif //DEBUG


	return 0;
}


void uploadDepthStencilBufferAndCreateView()
{
   
    D3D12_RESOURCE_DESC dsvTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, clientAreaWidth, clientAreaHeight, 1,
        1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0);
    D3D12_CLEAR_VALUE dsvTextureClearValue = {};
    dsvTextureClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    dsvTextureClearValue.DepthStencil = { 1.0f,0 };
    HANDLE_RETURN(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &dsvTextureDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &dsvTextureClearValue, IID_PPV_ARGS(&dsvResource)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D = { 0 };
    device->CreateDepthStencilView(dsvResource.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());

}










