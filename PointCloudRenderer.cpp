#include "PointCloudRenderer.h"


PointCloudRenderer::PointCloudRenderer()
{
    for (int i = 0; i < 2; ++i) {
        //HANDLE_RETURN(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
    }

}
