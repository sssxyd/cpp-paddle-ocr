# OCRWorker 测试用例

这个目录包含了 OCRWorker 类的完整测试用例。

## 文件说明

### 测试文件
- `test_ocr_worker.cpp` - 自包含的测试用例，不依赖外部测试框架

### 构建文件
- `build_tests.bat` - 构建脚本（Windows）

## 测试内容

测试用例覆盖了 OCRWorker 的以下功能：

1. **构造函数测试**
   - CPU 模式构造
   - GPU 模式构造（如果支持）
   - 无效模型路径处理

2. **生命周期管理**
   - 启动和停止
   - 重复启动处理
   - 多线程安全性

3. **OCR 处理功能**
   - 基本图像处理
   - 真实图像文件处理
   - 空图像处理
   - 小图像处理

4. **并发处理**
   - 多个请求并发处理
   - 队列管理

5. **状态管理**
   - 空闲状态检测
   - 工作状态管理

6. **性能测试**
   - 处理时间测量
   - 平均性能统计

## 如何运行测试

### 方法1：使用构建脚本（推荐）

这种方法简单易用，不依赖外部测试框架：

```batch
cd tests
build_tests.bat
cd build
test_ocr_worker.exe
```

### 方法2：使用 VS Code 任务

```batch
# 使用 VS Code 任务
Ctrl+Shift+P → Tasks: Run Task → run-ocr-tests
```

### 方法3：运行特定测试（调试时很有用）

```batch
cd tests\build
test_ocr_worker.exe BasicOCRProcessing
test_ocr_worker.exe ConstructorCPU
```

## 环境要求

### 必需组件
- Visual Studio 2019 或更新版本（需要 C++20 支持）
- OpenCV 4.x
- JsonCpp
- PaddlePaddle Inference

### 环境变量
确保以下环境变量已正确设置：
- `MSVC_INCLUDE` - MSVC 头文件路径
- `MSVC_LIB` - MSVC 库文件路径
- `WIN_SDK_INCLUDE` - Windows SDK 头文件路径
- `WIN_SDK_LIB` - Windows SDK 库文件路径
- `VCPKG_STATIC` - vcpkg 静态库路径

## 测试数据

测试用例会使用以下数据：

1. **生成的测试图像** - 包含简单文本的合成图像
2. **项目图像文件** - `images/title.jpg` 等真实图像文件
3. **边界情况** - 空图像、极小图像等

## 测试结果

### 成功的测试输出示例
```
Starting OCRWorker tests...
Current working directory: E:\LTS\rpa-windows-ocr\tests\build

=== Testing OCRWorker Constructor (CPU) ===
PASSED: OCRWorker constructor should not throw
PASSED: Worker should not be null
PASSED: Worker ID should be 1
PASSED: Worker should be idle initially

=== Testing Basic OCR Processing ===
PASSED: OCR processing should complete within 30 seconds
PASSED: Result JSON should not be empty
PASSED: Request ID should match
PASSED: Worker ID should match
PASSED: OCR should succeed
PASSED: Processing time should be positive

=== ALL TESTS PASSED ===
```

### 失败情况
如果测试失败，会显示详细的错误信息，包括：
- 失败的测试名称
- 期望值 vs 实际值
- 异常信息（如果有）

## 调试提示

1. **模型文件缺失** - 确保 `models` 目录包含必需的模型文件
2. **DLL 缺失** - 确保所有必需的 DLL 文件都在 PATH 中或测试目录中
3. **内存问题** - 如果遇到内存相关错误，检查图像数据的生命周期管理
4. **超时问题** - 如果 OCR 处理超时，可能是模型加载或 GPU 配置问题

## 扩展测试

你可以通过以下方式扩展测试：

1. **添加新的图像文件** - 在 `images` 目录中添加更多测试图像
2. **测试不同参数** - 修改 OCRWorker 构造参数进行测试
3. **压力测试** - 增加并发请求数量
4. **性能基准** - 添加更详细的性能测试

## 故障排除

### 常见问题

1. **编译错误**
   - 检查环境变量设置
   - 确保所有依赖库已正确安装
   - 验证 C++20 编译器支持

2. **运行时错误**
   - 检查 DLL 文件是否存在
   - 验证模型文件路径
   - 确认图像文件可访问

3. **测试失败**
   - 检查模型文件完整性
   - 验证 OCR 模型兼容性
   - 确认系统资源充足

如果遇到问题，请查看详细的错误日志并检查相关组件的安装状态。
