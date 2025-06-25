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
   - jsoncpp:x64-windows-static
5. 设置环境变量
   - VCPKG_STATIC: `E:\vcpkg\installed\x64-windows-static`

## Paddle_Inference
1. 从[官网](https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html#windows)下载Windows版预编译包
2. 将 paddle/include/* 复制到项目的 include/paddle_inference 目录下
3. 根据 [lib/msvc/README.md](./lib/msvc/README.md) 复制 lib
4. 根据 [bin/msvc/README.md](./bin/msvc/README.md) 复制 dll

## 主&子进程
1. 主业务启动OCR
   ```c++
   // 在主业务进程中
   std::string shutdown_event_name = "Global\\OCRServiceShutdown_" + std::to_string(GetCurrentProcessId());
   HANDLE shutdown_event = CreateEventA(NULL, TRUE, FALSE, shutdown_event_name.c_str());

   // 启动子进程
   std::string cmd = "ocr_service.exe --shutdown-event " + shutdown_event_name;
   PROCESS_INFORMATION pi;
   STARTUPINFOA si = {sizeof(si)};
   CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
   ```
2. 主业务关闭OCR
   ```c++
   // 在主业务进程退出时
   SetEvent(shutdown_event);  // 发送关闭信号
   WaitForSingleObject(pi.hProcess, 10000);  // 等待最多10秒
   TerminateProcess(pi.hProcess, 1);  // 强制终止（如果仍在运行）
   CloseHandle(shutdown_event);
   CloseHandle(pi.hProcess);
   CloseHandle(pi.hThread);
   ```