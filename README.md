# cpp-paddle-ocr
paddleOCR在办公电脑(无显卡)上实现100ms级的卡片识别IPC服务

# Usage
## 命令行
1. 启动OCR服务
   ```bash
    .\ocr-service.exe --help
    .\ocr-service.exe --cpu-workers 4
   ```
2. 识别图片
   ```bash
   .\ocr-client.exe --help
   .\ocr-client.exe ..\images\card-jd.jpg
   ```

## IPC调用
1. 启动OCR服务
2. 其他程序通过IPC调用该服务
   ```go
   package main

   import (
      "encoding/json"
      "fmt"
      "syscall"

      "golang.org/x/sys/windows"
   )

   type OcrRequest struct {
      Command   string `json:"command"`    // OCR命令
      ImageData string `json:"image_data"` // 图像数据，Base64编码
      ImagePath string `json:"image_path"` // 图像文件路径
   }

   func main() {
      // 使用默认的Named Pipe名称
      pipeName, err := syscall.UTF16PtrFromString(`\\.\pipe\ocr_service`)
      if err != nil {
         panic(err)
      }

      // 打开Named Pipe
      handle, err := windows.CreateFile(
         pipeName,
         windows.GENERIC_READ|windows.GENERIC_WRITE,
         0,
         nil,
         windows.OPEN_EXISTING,
         0,
         0,
      )
      if err != nil {
         fmt.Printf("打开Named Pipe失败: %v\n", err)
      }

      defer windows.CloseHandle(handle)

      fmt.Println("成功打开Named Pipe:", pipeName)

      // 构建请求, 这里改为你自己的图片地址
      image_path := "E:\\1755074161639.jpg"
      req := &OcrRequest{
         Command:   "recognize",
         ImagePath: image_path,
      }

      // 序列化请求
      reqData, err := json.Marshal(req)
      if err != nil {
         fmt.Printf("序列化请求失败: %v\n", err)
         return
      }

      // 发送请求
      var bytesWritten uint32
      err = windows.WriteFile(handle, reqData, &bytesWritten, nil)
      if err != nil {
         fmt.Printf("发送请求失败: %v\n", err)
         return
      }

      // 读取响应
      buffer := make([]byte, 64*1024) // 64K缓冲区
      var bytesRead uint32
      err = windows.ReadFile(handle, buffer, &bytesRead, nil)
      if err != nil {
         fmt.Printf("读取响应失败: %v\n", err)
         return
      }

      fmt.Printf("接收到响应:\n %s\n", string(buffer[:bytesRead]))

   }

   ```

# 环境配置
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
   - WIN_SDK_BIN: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64`

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
   - aklomp-base64:x64-windows-static
5. 设置环境变量
   - VCPKG_STATIC: `E:\vcpkg\installed\x64-windows-static`

## Paddle_Inference
1. 从[官网](https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html#windows)下载Windows版预编译包
2. 将 paddle/include/* 复制到项目的 include/paddle_inference 目录下
3. 根据 [lib/msvc/README.md](./lib/msvc/README.md) 复制 lib
4. 根据 [bin/msvc/README.md](./bin/msvc/README.md) 复制 dll

## Icon图片
1. rc文件编码，应当是：UTF-16 LE BOM
2. ps1文件里，不要用中文，搞不定