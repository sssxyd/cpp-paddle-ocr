#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include "ocr_worker.h"

namespace PaddleOCR {

/**
 * @brief GPU Worker Pool (支持单GPU多线程)
 */
class GPUWorkerPool {
public:
    GPUWorkerPool(const std::string& model_dir, int num_workers = 2);
    ~GPUWorkerPool();
    
    void start();
    void stop();
    std::future<std::string> submitRequest(std::shared_ptr<OCRRequest> request);
    
    int getOptimalWorkerCount();  // 根据GPU内存自动计算最优Worker数量
    
private:
    OCRWorker* getAvailableWorker();
    bool checkGPUMemory(int num_workers);  // 检查GPU内存是否足够
    
    std::vector<std::unique_ptr<OCRWorker>> workers_;
    std::mutex workers_mutex_;
    std::atomic<int> next_worker_index_;
    int gpu_memory_per_worker_mb_;
};

} // namespace PaddleOCR
