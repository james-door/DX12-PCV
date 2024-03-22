cmake_minimum_required (VERSION 3.8)

project ("Window")
set(CMAKE_CXX_STANDARD 17) #Use C++17




#Libraries for linking
set(LIBRARIES d3d12.lib dxgi.lib dxguid.lib dxcompiler.lib)

#Source files
set(SOURCE_FILES window.cpp )

add_executable (Window WIN32 ${SOURCE_FILES}) #Make a Windows GUI application





option(USE_UNICODE "Support Unicode." OFF)
if(USE_UNICODE)
	target_compile_definitions(Window PUBLIC UNICODE) 
	target_compile_definitions(Window PUBLIC _UNICODE) 
endif()


option(USE_DEBUG_LAYER "Toggles use of DXGI and Direct3D debug layers." OFF)
if(USE_DEBUG_LAYER)
	target_compile_definitions(Window PUBLIC DEBUG) 
endif()


option(USE_RUNTIME_SHADER_COMPILING "Whether to include the dxcompiler.dll." OFF)

if(USE_RUNTIME_SHADER_COMPILING)
    # Define shader directories
    set(SHADER_SOURCE_DIR "${PROJECT_SOURCE_DIR}/shaders")
    set(SHADER_DEST_DIR "${PROJECT_BINARY_DIR}/shaders")

    # Copying dxcompiler.dll
    set(DXC_COMPILER_DLL "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxcompiler.dll")
    add_custom_command(TARGET Window POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different
                       "${DXC_COMPILER_DLL}" "${PROJECT_BINARY_DIR}")
    
    # Copying dxil.dll if required
    set(DXIL_COMPILER_DLL "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxil.dll")
    add_custom_command(TARGET Window POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DXIL_COMPILER_DLL}" "${PROJECT_BINARY_DIR}")

    # Create a list of shader files to track changes
    file(GLOB_RECURSE SHADER_FILES "${SHADER_SOURCE_DIR}/*")

    # Add a custom target for shaders
    add_custom_target(Shaders ALL
                      COMMAND ${CMAKE_COMMAND} -E copy_directory
                      "${SHADER_SOURCE_DIR}" "${SHADER_DEST_DIR}"
                      COMMENT "Copying shaders to build directory")

    # Ensure the custom target is rebuilt when shaders are modified
    add_dependencies(Window Shaders) 
    # Treat the shader files like header files
    set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
endif()


#Windows SDK libraries
target_link_libraries(Window ${LIBRARIES})



target_include_directories(Window PRIVATE ${PROJECT_BINARY_DIR})


