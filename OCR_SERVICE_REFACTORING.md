# OCR Service 文件重构说明

## 概述

原来的 `ocr_service.cpp` 文件过大（约800行），已按类拆分为多个独立的头文件和源文件，提高了代码的可维护性和模块化程度。

## 新的文件结构

### 头文件 (include/paddle_ocr/)
1. **`ocr_service.h`** - 主头文件，包含所有其他头文件
2. **`ocr_worker.h`** - OCRWorker 类声明和 OCRRequest/OCRResult 结构体
3. **`gpu_worker_pool.h`** - GPUWorkerPool 类声明
4. **`cpu_worker_pool.h`** - CPUWorkerPool 类声明
5. **`ocr_ipc_service.h`** - OCRIPCService 类声明
6. **`ocr_ipc_client.h`** - OCRIPCClient 类声明

### 源文件 (src/)
1. **`ocr_service.cpp`** - 简化的主文件，仅包含必要的包含语句
2. **`ocr_worker.cpp`** - OCRWorker 类实现
3. **`gpu_worker_pool.cpp`** - GPUWorkerPool 类实现
4. **`cpu_worker_pool.cpp`** - CPUWorkerPool 类实现
5. **`ocr_ipc_service.cpp`** - OCRIPCService 类实现
6. **`ocr_ipc_client.cpp`** - OCRIPCClient 类实现

## 类职责划分

### OCRWorker
- 负责执行单个OCR任务
- 管理检测器、分类器、识别器
- 在独立线程中处理请求队列

### GPUWorkerPool
- 管理多个GPU工作线程
- 负载均衡和任务分发
- GPU内存管理

### CPUWorkerPool
- 管理多个CPU工作线程
- 负载均衡和任务分发

### OCRIPCService
- IPC服务端实现
- 硬件检测和配置
- 客户端连接管理

### OCRIPCClient
- IPC客户端实现
- 与服务端通信接口

## 使用方法

### 包含头文件
```cpp
#include "paddle_ocr/ocr_service.h"  // 包含所有功能
```

或者按需包含具体的头文件：
```cpp
#include "paddle_ocr/ocr_worker.h"
#include "paddle_ocr/ocr_ipc_client.h"
```

### 编译时需要包含的源文件
确保在编译时包含所有新的源文件：
- `ocr_worker.cpp`
- `gpu_worker_pool.cpp`
- `cpu_worker_pool.cpp`
- `ocr_ipc_service.cpp`
- `ocr_ipc_client.cpp`

## 优势

1. **代码组织**：每个类有自己的文件，便于维护
2. **编译效率**：修改单个类不需要重新编译整个大文件
3. **团队协作**：不同开发者可以同时修改不同的文件
4. **代码重用**：可以单独使用某个组件而不必包含整个服务
5. **测试友好**：便于对单个类进行单元测试

## 向后兼容性

现有代码仍然可以通过包含 `ocr_service.h` 来正常工作，无需修改。新的文件结构对外接口保持不变。
