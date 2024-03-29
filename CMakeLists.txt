cmake_minimum_required (VERSION 3.8)

project ("PCV")


set(CMAKE_CXX_STANDARD 17) #Use C++17

set(LIBRARIES d3d12.lib dxgi.lib dxguid.lib)
set(SOURCE_FILES PointCloudViewer.cpp PointCloudRenderer.cpp PointCloudRenderer.h debug.h)

add_executable(${CMAKE_PROJECT_NAME} WIN32 ${SOURCE_FILES} ) 
target_link_libraries(${CMAKE_PROJECT_NAME} ${LIBRARIES})

option(USE_UNICODE "Support Unicode." OFF)
if(USE_UNICODE)
	target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC UNICODE) 
	target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC _UNICODE) 
endif()

option(USE_DEBUG_LAYER "Toggles use of DXGI and Direct3D debug layers." OFF)

if(USE_DEBUG_LAYER)
	target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DEBUG) 
endif()

add_custom_target(compileVertexShader
    COMMAND dxc -T vs_6_0 -Fo ${PROJECT_BINARY_DIR}/vertexShader.dxil shaders/vertexShader.hlsl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS shaders/vertexShader.hlsl
    COMMENT "Compiling Vertex Shader."
)
add_custom_target(compileFragmentShader
    COMMAND dxc -T ps_6_0 -Fo ${PROJECT_BINARY_DIR}/fragmentShader.dxil shaders/fragmentShader.hlsl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	DEPENDS shaders/fragmentShader.hlsl
    COMMENT "Compiling Pixel Shader."
)

add_dependencies(${CMAKE_PROJECT_NAME} compileVertexShader compileFragmentShader)
