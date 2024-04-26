#pragma once

#include "debug.h"
#include <DXGI1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <dxgidebug.h>
#include "d3dx12.h" 
#include<DirectXMath.h>
#include <DirectXColors.h>


//C++
#include<filesystem>
#include<fstream>
#include <vector>
#include <optional>

struct PointCloudVertex {
	DirectX::XMFLOAT3 modelPos;
	DirectX::XMFLOAT3 colour;
	PointCloudVertex(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& col)
		: modelPos(pos), colour(col) {}
};

//State and functionality for point cloud.
//State and functionality for pipeline
//State for window
//State and functionality for render loop
//State and functionality for debug layers
class PointCloudRenderer
{
#if defined(DEBUG)
	UINT factoryDebug = DXGI_CREATE_FACTORY_DEBUG;
	Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> directDebugQueue;
#else
	UINT factoryDebug = 0;
#endif //DEBUG
	//Rendering State
	UINT rtvHeapOffset;
	Microsoft::WRL::ComPtr<ID3D12Device2> device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
	int activeBuffer = 0;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[2];
	Microsoft::WRL::ComPtr<ID3D12Resource> backBufferResources[2]; //Double buffer
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> backBufferDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> dsvResource;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
	//Window State
	HWND windowHandle;
	//Synchronisation State
	Microsoft::WRL::ComPtr<ID3D12Fence> swapChainFence;
	HANDLE swapChainPresentedEvent;
	UINT64 swapChainFenceValues[2] = {};
	UINT64 swapChainFenceValue = 0;
	//Pipeline State
	D3D12_VIEWPORT viewportDescription;
	D3D12_RECT scissorRec;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> PSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	//Camera State
	DirectX::XMMATRIX projectionMatrix;
	DirectX::XMMATRIX viewMatrix;
	DirectX::XMMATRIX modelMatrix;
	//Point Cloud State
	struct ViewingSphere
	{
		DirectX::XMFLOAT3 centre;
		float radius;
	};
	ViewingSphere viewingSphere;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	unsigned int nVerts;

	void initDirect3D();
	void uploadPointCloudDataToGPU(const std::vector<PointCloudVertex>& vertices);
	void createPointCloudPipeline();
	std::optional<std::vector<std::byte>> loadByteCode(std::filesystem::path path);
	void CalculateMinimumBoundingSphere(const std::vector<PointCloudVertex>& vertices);

public:
	//Exposed Camera state
	float FOV = 45.0f;
	float yaw = 0.0f;
	float pitch = 0.0f;
	int oldMousePosX = 0.0f;
	int oldMousePosY = 0.0f;
	bool leftMouseButtonHeld = false;
	DirectX::XMMATRIX MVP;
	std::chrono::steady_clock::time_point previousFrameTime;
	std::chrono::duration<float, std::ratio<1, 1>> deltaTime;
	bool firstMove = true;
	//Exposed Rendering State
	UINT rtvWidth;
	UINT rtvHeight;
	BOOL screenTearingEnabled;
	bool fsbw;
	RECT previousClientArea;

	~PointCloudRenderer();
	PointCloudRenderer(HWND windowHandle, UINT rtvWidth, UINT rtvHeight, BOOL screenTearingEnabled,
		std::shared_ptr<std::vector<PointCloudVertex>> pointCloudVertices);
	void flushGPU();
	void uploadNewDepthStencilBufferAndCreateView(UINT newWidth, UINT newHeight);
	void resizeRenderTargetView(UINT newWidth, UINT newHeight);
	void PointCloudRenderer::resizeViewPort(UINT newWidth, UINT newHeight);
	void recaculateMVP();
	void draw();
#if defined(DEBUG)
	void outputDebugLayer();
#endif
};