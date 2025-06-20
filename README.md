# rpa-windows-ocr

## cgo环境
1. windows上安装：[mysys2](https://www.msys2.org/), 安装目录：D:\Applications\msys64
2. mysys2 shell 安装ucrt64：`pacman -S mingw-w64-ucrt-x86_64-gcc`
3. ucrt64 shell 执行：`pacman -Syu`
4. 将 `D:\Applications\msys64\ucrt64\bin` 路径设置到windows的Path
5. 工具链：`pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-git`
6. 配置proxy
    ```shell
    git config --global https.proxy http://127.0.0.1:1080
    export https_proxy="http://127.0.0.1:1080"
    ```



## paddle inference 导出
1. 安装swig, `pacman -S mingw-w64-ucrt-x86_64-swig`
2. 下载 [paddle inference 3.0.0 预编译包](https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html#windows)
3. 解压到工程目录下的 paddle_inference
4. third_pary下，只保留：mklml/onednn/protobuf/utf8proc
5. 解决protobuf版本问题
   ```shell
    # pacman -S mingw-w64-ucrt-x86_64-abseil-cpp
    # pacman -S mingw-w64-ucrt-x86_64-gtest 
    pacman -S mingw-w64-ucrt-x86_64-protobuf
   ```
6. 安装工具、执行脚本，将msvc编译的动态/静态链接库转为mingw的动态/静态链接库
   ```shell
   pacman -S mingw-w64-ucrt-x86_64-tools-git
   pacman -S mingw-w64-ucrt-x86_64-binutils
   sh convert_to_mingw_libs.sh
   ```
7. 配置c++标准为 c++20
8. copy paddle_inference/paddle/include --> /ucret64/include/paddle_inference
9. copy paddle_inference/paddle/lib/*.a  paddle_inference/third_party/*/*.a --> /ucret64/lib
10. copy paddle_inference/paddle/lib/*.dll  paddle_inference/third_party/*/*.dll --> /ucret64/bin
   
   