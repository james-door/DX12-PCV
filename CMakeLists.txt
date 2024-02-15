cmake_minimum_required (VERSION 3.8)

project ("Window")
set(CMAKE_CXX_STANDARD 17) #Use C++17
add_executable (Window WIN32 "Window.cpp") #Make a Windows GUI application

#Windows SDK libraries
target_link_libraries(Window  d3d12.lib dxgi.lib dxguid.lib)

target_include_directories(Window PRIVATE ${PROJECT_BINARY_DIR})
option(USE_UNICODE "Support Unicode." OFF)
if(USE_UNICODE)
	target_compile_definitions(Window PUBLIC UNICODE) 
	target_compile_definitions(Window PUBLIC _UNICODE) 
endif()



