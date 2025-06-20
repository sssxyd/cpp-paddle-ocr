# rpa-windows-ocr

## MSVC环境
1. 安装 [Visual Studio Community 2022](https://visualstudio.microsoft.com/zh-hans/downloads/) && C++ && Windows SDK Kit
2. 安装 [NASM](https://www.nasm.us/)、[CMake](https://cmake.org/download/)
3. 将 cl.exe/nasm.exe/camke.exe 所在目录加入PATH
4. 设置环境变量
   - MSVC_INCLUDE: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include`
   - MSVC_LIB: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64`
   - MSVC_BIN: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64`
   - WIN_SDK_INCLUDE: `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0`
   - WIN_SDK_LIB: `C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0`

## VCPKG环境
1. 安装vcpkg `git clone https://github.com/microsoft/vcpkg.git`
2. 生成vcpkg.exe `.\bootstrap-vcpkg.bat`
3. 将vcpkg.exe 所在路径加入 PATH
4. 安装静态链接库(admin启动powershell并设置代理)
   - protobuf:x64-windows-static
   - opencv:x64-windows-static
   - glog:x64-windows-static
   - gflags:x64-windows-static
5. 设置环境变量
   - VCPKG_STATIC: `E:\vcpkg\installed\x64-windows-static`

## Paddle_Inference
1. 从[官网](https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html#windows)下载Windows版预编译包
2. 将 paddle/include/* 复制到项目的 include/paddle_inference 目录下
3. 根据 lib/msvc/README.md 复制 lib
4. 根据 bin/msvc/README.md 复制 dll