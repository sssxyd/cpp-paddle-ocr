# Usage
1. 清理build
   ```batch
   # 使用 VS Code 任务
   Ctrl+Shift+P → Tasks: Run Task → clean-test-build
   ```
2. 编译tests
   ```batch
   # 使用 VS Code 任务
   Ctrl+Shift+P → Tasks: Run Task → build-ocr-client
   ```
3. 运行tests
   ```batch
   # 使用 VS Code 任务
   Ctrl+Shift+P → Tasks: Run Task → run-ocr-tests
   ```

4. debug tests  
使用vscode的Run And Debug，运行Debug任务

# 必需组件
- Visual Studio 2019 或更新版本（需要 C++20 支持）
- OpenCV 4.x
- JsonCpp
- PaddlePaddle Inference

# 环境变量
确保以下环境变量已正确设置：
- `MSVC_INCLUDE` - MSVC 头文件路径
- `MSVC_LIB` - MSVC 库文件路径
- `WIN_SDK_INCLUDE` - Windows SDK 头文件路径
- `WIN_SDK_LIB` - Windows SDK 库文件路径
- `VCPKG_STATIC` - vcpkg 静态库路径
