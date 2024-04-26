# Point Cloud Viewer

Create a simple point cloud viewer for Windows using DirectX 12.

## Build

The project's `CMakePresets.json` has been created to be built using a [CMake project in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170). Because in its current form it requires the Visual Studio state variables for the MSVC compiler. I found that this version of Microsoft's helper header, `d3dx12.h`, fails to compile using Clang version 15.0.6 targeting x86_64-pc-windows-msvc.

## Run

The point cloud viewer is used through a command-line interface and can be used to display a single ASCII point cloud using the following command:
```bash
pcv.exe <name-of-point-cloud>