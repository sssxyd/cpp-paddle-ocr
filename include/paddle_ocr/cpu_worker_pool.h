#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include "ocr_worker.h"

namespace PaddleOCR {

/**
 * @brief CPU Worker Pool
 */
class CPUWorkerPool {
public:
    CPUWorkerPool(const std::string& model_dir, int num_workers);
    ~CPUWorkerPool();
    
    void start();
    void stop();
    std::future<std::string> submitRequest(std::shared_ptr<OCRRequest> request);
    
private:
    OCRWorker* getAvailableWorker();
    
    std::vector<std::unique_ptr<OCRWorker>> workers_;
    std::mutex workers_mutex_;
    std::atomic<int> next_worker_index_;
};

} // namespace PaddleOCR
