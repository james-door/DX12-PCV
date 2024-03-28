#include "PointCloudRenderer.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX;

PointCloudRenderer::PointCloudRenderer(HWND windowHandle, UINT rtvWidth, UINT rtvHeight, BOOL screenTearingEnabled,
    std::shared_ptr<std::vector<PointCloudVertex>> pointCloudVertices)
{
    PointCloudRenderer::windowHandle = windowHandle;
    PointCloudRenderer::rtvWidth = rtvWidth;
    PointCloudRenderer::rtvHeight = rtvHeight;
    PointCloudRenderer::screenTearingEnabled = screenTearingEnabled;
    PointCloudRenderer::fsbw = false;
    nVerts = pointCloudVertices->size();

    initDirect3D();
    createPointCloudPipeline();
    CalculateMinimumBoundingSphere(*pointCloudVertices);
    uploadPointCloudDataToGPU(*pointCloudVertices);

    //DeltaTime
    previousFrameTime = std::chrono::steady_clock::now();

}

void PointCloudRenderer::draw()
{
    ++swapChainFenceValue;
    allocators[activeBuffer]->Reset();
    cmdList->Reset(allocators[activeBuffer].Get(), NULL);

    cmdList->SetPipelineState(PSO.Get());
    cmdList->SetGraphicsRootSignature(rootSignature.Get());
    cmdList->RSSetViewports(1, &viewportDescription);
    cmdList->RSSetScissorRects(1, &scissorRec);

    auto activeBackBufferDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(backBufferDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        activeBuffer, rtvHeapOffset);
    auto dsvDescriptorHeapHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart());

    cmdList->OMSetRenderTargets(1, &activeBackBufferDescriptorHandle, FALSE, &dsvDescriptorHeapHandle);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(MVP) / 4, &MVP, 0);

    CD3DX12_RESOURCE_BARRIER present2RTV = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    CD3DX12_RESOURCE_BARRIER RTV2Present = CD3DX12_RESOURCE_BARRIER::Transition(backBufferResources[activeBuffer].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    cmdList->ResourceBarrier(1, &present2RTV);

    cmdList->ClearRenderTargetView(activeBackBufferDescriptorHandle, Colors::Black, 0, NULL);
    cmdList->ClearDepthStencilView(dsvDescriptorHeapHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    cmdList->DrawInstanced(nVerts, 1, 0, 0);

    cmdList->ResourceBarrier(1, &RTV2Present);
    cmdList->Close();
    ID3D12CommandList* cmdLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    swapChain->Present((screenTearingEnabled) ? 0 : 1,
        (screenTearingEnabled) ? DXGI_PRESENT_ALLOW_TEARING : 0);

    swapChainFenceValues[activeBuffer] = swapChainFenceValue;
    cmdQueue->Signal(swapChainFence.Get(), swapChainFenceValues[activeBuffer]);

    activeBuffer = swapChain->GetCurrentBackBufferIndex();
    swapChainFence->SetEventOnCompletion(swapChainFenceValues[activeBuffer], swapChainPresentedEvent);
    WaitForSingleObject(swapChainPresentedEvent, INFINITE);
}

void PointCloudRenderer::uploadPointCloudDataToGPU(const std::vector<PointCloudVertex>& vertices)
{
    UINT64 bufferSize = sizeof(PointCloudVertex) * vertices.size();

    D3D12_SUBRESOURCE_DATA vertexSubResourceData = {};
    vertexSubResourceData.pData = vertices.data();
    vertexSubResourceData.RowPitch = bufferSize;
    vertexSubResourceData.SlicePitch = vertexSubResourceData.RowPitch;

    //Transfer data to VRAM
    ComPtr<ID3D12Resource> vertexStagingBufferResource;
    D3D12_RESOURCE_DESC vertexResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

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

    flushGPU();

    //Create vertex buffer view
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = bufferSize;
    vertexBufferView.StrideInBytes = sizeof(PointCloudVertex);


}

void PointCloudRenderer::createPointCloudPipeline()
{

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1; 
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HANDLE_RETURN(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));

    //Create heap and resource for depth-stencil buffer
    uploadNewDepthStencilBufferAndCreateView(rtvWidth,rtvHeight);

    std::optional<std::vector<std::byte>>  vertexShaderByteCode = loadByteCode("vertexShader.dxil");
    std::optional<std::vector<std::byte>>  pixelShaderByteCode = loadByteCode("fragmentShader.dxil");
    if (!vertexShaderByteCode.has_value() || !pixelShaderByteCode.has_value())
        throw std::runtime_error("Failed to load shaders.");





    //Setup for Rasteriser state

    //Viewport
    viewportDescription = {};
    viewportDescription.TopLeftX = 0;
    viewportDescription.TopLeftY = 0;
    viewportDescription.Width = rtvWidth;
    viewportDescription.Height = rtvHeight;
    viewportDescription.MinDepth = D3D12_MIN_DEPTH;
    viewportDescription.MaxDepth = D3D12_MAX_DEPTH;

    //Scissor Rectangle
    scissorRec = {};
    scissorRec.top = 0;
    scissorRec.left = 0;
    scissorRec.bottom = LONG_MAX;
    scissorRec.right = LONG_MAX;



    //PSO

    //Input layout

    D3D12_INPUT_ELEMENT_DESC vertexColourPositionLayoutDescription[2] =
    {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };

    D3D12_INPUT_LAYOUT_DESC layoutDescription = { vertexColourPositionLayoutDescription,2 };

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
    rootSignatureParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(1, rootSignatureParameters, 0, 0, rootSignatureFlags);



    ComPtr<ID3DBlob> seralisedRootSignature;
    ComPtr<ID3DBlob> seralisedRootSignatureErrors;

    //Check shader model feature support
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureFeatureLevel = {};
    rootSignatureFeatureLevel.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureFeatureLevel, sizeof(rootSignatureFeatureLevel))))
    {
        rootSignatureFeatureLevel.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; //if the feature check fails, set it to version 1.1
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
    stream.vs = CD3DX12_PIPELINE_STATE_STREAM_VS(CD3DX12_SHADER_BYTECODE(vertexShaderByteCode->data(), vertexShaderByteCode->size()));
    stream.ps = CD3DX12_PIPELINE_STATE_STREAM_PS(CD3DX12_SHADER_BYTECODE(pixelShaderByteCode->data(), pixelShaderByteCode->size()));
    stream.inputLayout = CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT(layoutDescription);
    stream.primitive = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    stream.targetFormat = rtvFormat;
    stream.rootSig = rootSignature.Get();
    stream.dsFormat = DXGI_FORMAT_D32_FLOAT;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStream{};
    pipelineStateStream.pPipelineStateSubobjectStream = &stream;
    pipelineStateStream.SizeInBytes = sizeof(StateStream);
    HANDLE_RETURN(device->CreatePipelineState(&pipelineStateStream, IID_PPV_ARGS(&PSO)));
}

void PointCloudRenderer::flushGPU()
{
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
    cmdQueue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
}

void PointCloudRenderer::initDirect3D()
{
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
    while(factory->EnumAdapters1(n, &adapter) == S_OK)
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
        D3D12_INFO_QUEUE_FILTER_DESC filterDesc = {};
        D3D12_MESSAGE_SEVERITY allowedMessages[] = { D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR, D3D12_MESSAGE_SEVERITY_WARNING };
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
    swapChainDesc.Width = rtvWidth;
    swapChainDesc.Height = rtvHeight;
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
        heapHandle.Offset(rtvHeapOffset * i);

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
}


void PointCloudRenderer::uploadNewDepthStencilBufferAndCreateView(UINT width, UINT height)
{

    D3D12_RESOURCE_DESC dsvTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1,
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


void PointCloudRenderer::resizeRenderTargetView(UINT newWidth, UINT newHeight)
{
    for (int i = 0; i < 2; ++i) {
        backBufferResources[i].Reset();
        swapChainFenceValues[i] = swapChainFenceValue;
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
void PointCloudRenderer::resizeViewPort(UINT newWidth, UINT newHeight)
{
    viewportDescription.Width = newWidth;
    viewportDescription.Height = newHeight;
}

void PointCloudRenderer::recaculateMVP()
{
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
    float aspectRatio = static_cast<float>(rtvWidth) / static_cast<float>(rtvHeight);
    projectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(FOV), aspectRatio, 0.1, 100);
    MVP = XMMatrixMultiply(XMMatrixMultiply(modelMatrix, viewMatrix), projectionMatrix);

}



void PointCloudRenderer::CalculateMinimumBoundingSphere(const std::vector<PointCloudVertex>& vertices) {
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
        XMStoreFloat(&distanceValue, distance);
        maxDistance = std::max(maxDistance, distanceValue);

    }

    viewingSphere = { centroid,maxDistance };
}



std::optional<std::vector<std::byte>> PointCloudRenderer::loadByteCode(std::filesystem::path path)
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

PointCloudRenderer::~PointCloudRenderer() {
    flushGPU();
    if (swapChainPresentedEvent) {
        CloseHandle(swapChainPresentedEvent);
        swapChainPresentedEvent = nullptr;
    }
#if defined(DEBUG)
    outputDebugLayer();
#endif

}

#if defined(DEBUG)
void PointCloudRenderer::outputDebugLayer(){
    //DXGI
    UINT64 numMsg;
    numMsg = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_DX);
    for (UINT64 i = 0; i < numMsg; ++i) {
        SIZE_T messageSize;
        dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, nullptr, &messageSize);
        DXGI_INFO_QUEUE_MESSAGE* msg = (DXGI_INFO_QUEUE_MESSAGE*)malloc(messageSize);
        dxgiInfoQueue->GetMessage(DXGI_DEBUG_DX, i, msg, &messageSize);
        if (msg)
            displayErrorMessage("DXGI Error:\n\n" + std::string(msg->pDescription));
    }


//D3D12

    numMsg = directDebugQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < numMsg; ++i) {
        SIZE_T messageSize;
        directDebugQueue->GetMessage(i, nullptr, &messageSize);
        D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(messageSize);
        directDebugQueue->GetMessage(i, msg, &messageSize);
        if (msg)
            displayErrorMessage("D3D Errors:\n\n" + std::string(msg->pDescription));
    }
}
#endif //DEBUG