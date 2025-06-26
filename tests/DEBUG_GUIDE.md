# OCRWorker 测试调试指南

## 🐛 如何调试测试用例

### 方法1：使用 VS Code 调试器（推荐）

1. **设置断点**：
   - 在你想要调试的代码行左侧点击，设置红色断点
   - 可以在测试函数内部、OCRWorker 类方法内部设置断点

2. **启动调试**：
   - 按 `F5` 或 `Ctrl+F5`
   - 或者按 `Ctrl+Shift+P` → "Debug: Start Debugging"
   - 选择 "Debug OCR Tests (Simple)" 配置

3. **调试控制**：
   - `F10` - 单步跳过 (Step Over)
   - `F11` - 单步进入 (Step Into)  
   - `Shift+F11` - 单步跳出 (Step Out)
   - `F5` - 继续执行 (Continue)
   - `Shift+F5` - 停止调试 (Stop)

### 方法2：在代码中添加调试输出

在测试代码中添加详细的输出信息：

```cpp
// 在测试函数中添加调试输出
void testBasicOCRProcessing() {
    std::cout << "\n=== 开始基本OCR处理测试 ===" << std::endl;
    
    worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
    std::cout << "Worker创建成功，ID: " << worker_->getWorkerId() << std::endl;
    
    worker_->start();
    std::cout << "Worker启动成功" << std::endl;
    
    auto request = std::make_shared<OCRRequest>(1001, test_image_);
    std::cout << "创建请求，ID: " << request->request_id << std::endl;
    std::cout << "图像尺寸: " << test_image_.cols << "x" << test_image_.rows << std::endl;
    
    // ... 其他代码
}
```

### 方法3：使用条件断点

1. **设置条件断点**：
   - 右键点击断点 → "Edit Breakpoint..."
   - 添加条件，如 `request_id == 1001`
   - 只有满足条件时才会停止

### 方法4：调试特定测试用例

如果使用 Google Test 版本，可以只运行特定测试：

```cpp
// 在 launch.json 中设置 args
"args": [
    "--gtest_filter=OCRWorkerTest.BasicOCRProcessing",
    "--gtest_break_on_failure"
]
```

## 🔍 常见调试场景

### 1. 调试 OCRWorker 构造函数

```cpp
void testConstructorCPU() {
    std::cout << "模型目录: " << model_dir_ << std::endl;
    
    // 在这里设置断点，检查模型路径是否正确
    worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
    
    std::cout << "Worker创建成功" << std::endl;
}
```

**调试要点**：
- 检查 `model_dir_` 是否指向正确的模型目录
- 确认模型文件是否存在
- 查看异常信息（如果有）

### 2. 调试 OCR 处理流程

```cpp
void testBasicOCRProcessing() {
    // 在 addRequest 前设置断点
    worker_->addRequest(request);
    
    // 在 future.wait 前设置断点，检查请求是否正确添加
    auto status = future.wait_for(std::chrono::seconds(30));
    
    // 检查处理结果
    std::string result_json = future.get();
    std::cout << "OCR结果: " << result_json << std::endl;
}
```

**调试要点**：
- 检查图像数据是否正确加载
- 确认请求是否正确添加到队列
- 查看 OCR 处理结果的 JSON 格式

### 3. 调试并发处理

```cpp
void testConcurrentProcessing() {
    // 在循环中设置断点
    for (int i = 0; i < num_requests; ++i) {
        auto request = std::make_shared<OCRRequest>(2000 + i, test_image_);
        std::cout << "创建请求 " << i << ", ID: " << (2000 + i) << std::endl;
        
        // 设置断点检查每个请求
        worker_->addRequest(request);
    }
}
```

**调试要点**：
- 确认所有请求都被正确创建
- 检查请求处理的顺序
- 验证并发安全性

## 💡 调试技巧

### 1. 使用监视窗口

在调试时，可以监视重要变量：
- `worker_->getWorkerId()`
- `worker_->isIdle()`
- `test_image_.cols` 和 `test_image_.rows`
- `result_json`

### 2. 检查异常信息

```cpp
try {
    worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
} catch (const std::exception& e) {
    std::cerr << "异常: " << e.what() << std::endl;
    // 在这里设置断点查看详细异常信息
}
```

### 3. 验证模型文件

```cpp
void setUp() {
    model_dir_ = "models";
    
    // 调试时检查模型文件
    std::cout << "检查模型文件..." << std::endl;
    std::ifstream det_model("models/det/inference.pdmodel");
    std::ifstream rec_model("models/rec/inference.pdmodel");
    std::ifstream cls_model("models/cls/inference.pdmodel");
    
    std::cout << "det模型存在: " << det_model.good() << std::endl;
    std::cout << "rec模型存在: " << rec_model.good() << std::endl;
    std::cout << "cls模型存在: " << cls_model.good() << std::endl;
}
```

### 4. 图像调试

```cpp
cv::Mat createTestImage() {
    cv::Mat image = cv::Mat::zeros(200, 600, CV_8UC3);
    image.setTo(cv::Scalar(255, 255, 255));
    
    // 添加文本...
    
    // 调试时保存图像
    cv::imwrite("debug_test_image.png", image);
    std::cout << "测试图像已保存到 debug_test_image.png" << std::endl;
    
    return image;
}
```

## 🚨 常见问题和解决方法

### 1. 模型加载失败
- 检查模型文件路径
- 确认模型文件完整性
- 验证模型格式兼容性

### 2. 内存访问错误
- 检查图像数据的生命周期
- 确认智能指针的正确使用
- 验证多线程安全性

### 3. 超时问题
- 增加超时时间进行调试
- 检查 OCR 处理是否卡在某个环节
- 确认线程是否正常工作

### 4. JSON 解析错误
- 打印原始 JSON 字符串
- 检查 JSON 格式是否正确
- 验证必需字段是否存在

## 📝 调试最佳实践

1. **逐步调试** - 从简单的测试开始，逐步增加复杂性
2. **详细日志** - 添加足够的调试输出信息
3. **异常处理** - 确保所有异常都被正确捕获和处理
4. **数据验证** - 在关键点验证数据的正确性
5. **资源管理** - 确保资源被正确释放

通过这些调试方法，你可以深入了解测试的执行过程，快速定位和解决问题。
