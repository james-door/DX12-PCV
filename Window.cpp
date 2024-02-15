
//Windows 
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//C++
#include <vector>
#include <algorithm>

#include <sstream>
#include <tchar.h>
#include <wrl.h>

// Helper header 
#include "d3dx12.h" 

//DirectX 12

//Direct3D
#include <d3d12.h>
#include <DXGI1_6.h>

//DXGI
#include <dxgidebug.h>


using Microsoft::WRL::ComPtr;


// DEBUG
#define DEBUG

#if defined(DEBUG)
UINT factoryDebug = DXGI_CREATE_FACTORY_DEBUG;

ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
ComPtr<ID3D12InfoQueue> directDebugQueue;


#endif //DEBUG


#define HANDLE_RETURN(err) LogIfFailed(err, __FILE__, __LINE__)
std::stringstream Win32ErrorLog;
std::stringstream DirectErrorLog;
inline void LogIfFailed(HRESULT err, const char* file, int line);
inline void LogIfFailed(bool err, const char* file, int line);
inline std::string HrToString(HRESULT hr);
void displayErrorMessage(std::string error);
void logDebug();


//Globals
LONG clientAreaWidth;
LONG clientAreaHeight;

bool d3dInit = false;
bool screenTearingEnabled = true;
bool fsbw= false;
RECT previousClientArea;
ComPtr<ID3D12CommandAllocator> allocators[2];
ComPtr<ID3D12GraphicsCommandList> cmdList;
ComPtr<ID3D12Resource> backBufferResources[2];
UINT rtvHeapOffset;
ComPtr<ID3D12DescriptorHeap> backBufferDescriptorHeap;
ComPtr<ID3D12CommandQueue> cmdQueue;
ComPtr<IDXGISwapChain3> swapChain;

ComPtr<ID3D12Fence> fence;
HANDLE presentedEvent;
UINT64 fenceValues[2] = {};
UINT64 fenceValue = 0;
int activeBuffer = 0;

ComPtr<ID3D12Device2> device;

//Message Procedure
LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (d3dInit) {
        switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_PAINT:
            ++fenceValue;

            allocators[activeBuffer]->Reset();
            cmdList->Reset(allocators[activeBuffer].Get(), NULL);

            //Clear the target
            //By default back buffer resources created in the swap chain are in the state D3D12_RESOURCE_STATE_PRESENT
            //To clear them they must be in the state D3D12_RESOURCE_STATE_RENDER_TARGET 
            {
                CD3DX12_RESOURCE_BARRIER present2RTV = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

                CD3DX12_RESOURCE_BARRIER RTV2Present = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                cmdList->ResourceBarrier(1, &present2RTV);

                auto bufferDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(backBufferDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                    activeBuffer, rtvHeapOffset);
                float clearColour[] = { 1.0f,0.0f,0.0f,0.0f };
                cmdList->ClearRenderTargetView(bufferDescriptorHandle, clearColour, 0, NULL);
                cmdList->ResourceBarrier(1, &RTV2Present);
            }

            cmdList->Close();

            {
                ID3D12CommandList* cmdLists[] = { cmdList.Get() };
                cmdQueue->ExecuteCommandLists(1, cmdLists);
                swapChain->Present((screenTearingEnabled) ? 0 : 1,
                    (screenTearingEnabled) ? DXGI_PRESENT_ALLOW_TEARING : 0);
            }

            fenceValues[activeBuffer] = fenceValue;
            cmdQueue->Signal(fence.Get(), fenceValues[activeBuffer]);

            activeBuffer = swapChain->GetCurrentBackBufferIndex();
            fence->SetEventOnCompletion(fenceValues[activeBuffer], presentedEvent);
            WaitForSingleObject(presentedEvent, INFINITE);

            break;
        case WM_SIZE:
            
        {
            RECT newClientArea = {};
            HANDLE_RETURN(GetClientRect(hWnd, &newClientArea) == 0);
            LONG newWidth = std::max(newClientArea.right - newClientArea.left, 1L);
            LONG newHeight = std::max(newClientArea.bottom - newClientArea.top, 1L);
            if (newWidth == clientAreaWidth && newHeight == clientAreaHeight)
                break;
            //Flush GPU
            ComPtr<ID3D12Fence> flush;
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&flush));
            cmdQueue->Signal(flush.Get(), 1);
            HANDLE flushEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            flush->SetEventOnCompletion(1, flushEvent);
            WaitForSingleObject(flushEvent, INFINITE);

            for (int i = 0; i < 2; ++i) {
                backBufferResources[i].Reset();
                fenceValues[i] = fenceValue;
            }
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
                        SetWindowLongPtr(hWnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW ^ (WS_OVERLAPPED | WS_CAPTION  | WS_SYSMENU  | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)));
                        HANDLE_RETURN(GetWindowRect(hWnd, &previousClientArea) == 0);
                        HMONITOR nearestDisplay = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO display = {};
                        display.cbSize = sizeof(MONITORINFO);
                        HANDLE_RETURN(GetMonitorInfo(nearestDisplay, &display) == 0);
                        newDisplay = display.rcMonitor;
                    }
                    LONG displayWidth = newDisplay.right - newDisplay.left;
                    LONG displayHeight = newDisplay.bottom - newDisplay.top;

                    SetWindowPos(hWnd, HWND_TOPMOST, newDisplay.left, newDisplay.top, displayWidth, displayHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    fsbw = !fsbw;
                }
                break;
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;

            }

        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
    }
    else 
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
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
    constexpr LONG defaultClientAreaWidth = 990;
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
    ComPtr<ID3D12Debug> debugLayer;
    HANDLE_RETURN(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)));
    debugLayer->EnableDebugLayer();

    //DXGI info queue
    if (SUCCEEDED(DXGIGetDebugInterface1(NULL, IID_PPV_ARGS(&dxgiInfoQueue)))) {
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_DX, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);

        //Create filter
        DXGI_INFO_QUEUE_FILTER_DESC dxgiFilterDesc = {};
        DXGI_INFO_QUEUE_MESSAGE_SEVERITY dxgiAllowedSeverity[] = { DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR };
        dxgiFilterDesc.NumSeverities = 2;
        dxgiFilterDesc.pSeverityList = dxgiAllowedSeverity;
        DXGI_INFO_QUEUE_FILTER dxgiFilter = {};
        dxgiFilter.AllowList = { dxgiFilterDesc };
        dxgiInfoQueue->PushStorageFilter(DXGI_DEBUG_DX,&dxgiFilter);
    }

#endif //DEBUG

    //Choose adapter

    ComPtr<IDXGIFactory4> factory;
    HANDLE_RETURN(CreateDXGIFactory2(factoryDebug, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIAdapter1> bestAdapter;
    UINT n = 0;
    UINT highestVRam = -1;
    //Choose an adapter
    while (factory->EnumAdapters1(n, &adapter) == S_OK)
    {
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        HANDLE_RETURN(adapter->GetDesc1(&adapterDesc));
        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
            && D3D12CreateDevice(adapter.Get(), featureLevel, __uuidof(ID3D12Device2), NULL) == S_FALSE //Check adaptor feature level is high enough
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
        D3D12_MESSAGE_SEVERITY allowedMessages[] = {D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR};
        filterDesc.NumSeverities = 2;
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
    HANDLE_RETURN(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    presentedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);


    d3dInit = true;
    

    ShowWindow(windowHandle, SW_SHOW);

    //Message Loop
    MSG windowMsg= {};
    while (windowMsg.message != WM_QUIT) {
        GetMessage(&windowMsg, NULL, 0, 0);
        TranslateMessage(&windowMsg);
        DispatchMessage(&windowMsg);
    }

    logDebug();


	return 0;
}



void logDebug() {
    //DX12 info queue
    if (!Win32ErrorLog.str().empty()) {
        displayErrorMessage("Win32 Errors:\n\n" + Win32ErrorLog.str());
    }
    if (!DirectErrorLog.str().empty()) {
        displayErrorMessage("DX12 Errors:\n\n" + DirectErrorLog.str());
    }

#if defined(DEBUG)
    //DXGI
    {
        UINT64 numMsg = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_DX);
        for (UINT64 i = 0; i < numMsg; ++i) {
            SIZE_T messageSize;
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, nullptr, &messageSize);
            DXGI_INFO_QUEUE_MESSAGE* msg = (DXGI_INFO_QUEUE_MESSAGE*)malloc(messageSize);
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, msg, &messageSize);
            displayErrorMessage("DXGI Error:\n\n" + std::string(msg->pDescription));
        }
    }

    //D3D12
    {
        UINT64 numMsg = directDebugQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < numMsg; ++i) {
            SIZE_T messageSize;
            directDebugQueue->GetMessage(i, nullptr, &messageSize);
            D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(messageSize);
            directDebugQueue->GetMessage(i, msg, &messageSize);
            displayErrorMessage("D3D Errors:\n\n" + std::string(msg->pDescription));
        }
    }
#endif //DEBUG
}





// DEBUG
inline void LogIfFailed(HRESULT err, const char* file, int line) {
    if (FAILED(err)) {
        std::string message = "File: " + std::string(file) + "\n\n" + HrToString(err) + "\n\nLine: " + std::to_string(line);
        DirectErrorLog << message;
    }
}


inline void LogIfFailed(bool err, const char* file, int line)
{
    if (err)
    {
        DWORD error = GetLastError();
        LPVOID lpMsgBuf;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0, NULL);


        std::string message = "File: " + std::string(file) + "\n\nMessage: " + std::string((char*)lpMsgBuf) + "\n\nLine: " + std::to_string(line);
        LocalFree(lpMsgBuf);

        Win32ErrorLog << message;
    }
}

inline std::string HrToString(HRESULT hr)
{
    char s_str[64] = {};
    sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(s_str);
}
void displayErrorMessage(std::string error) {

    MessageBoxA(NULL, error.c_str(), NULL, MB_OK);

}