#include "paddle_ocr/gpu_worker_pool.h"
#include <iostream>
#include <algorithm>

namespace PaddleOCR {

// GPUWorkerPool 实现
GPUWorkerPool::GPUWorkerPool(const std::string& model_dir, int num_workers) 
    : next_worker_index_(0), gpu_memory_per_worker_mb_(1500) {
    
    // 检查GPU内存是否足够
    if (!checkGPUMemory(num_workers)) {
        num_workers = std::max(1, num_workers / 2);  // 减少Worker数量
        std::cout << "GPU memory limited, reducing workers to: " << num_workers << std::endl;
    }
    
    workers_.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(std::make_unique<OCRWorker>(
            i, model_dir, true, 0  // 所有Worker使用GPU 0
        ));
    }
    
    std::cout << "GPUWorkerPool created with " << num_workers << " workers" << std::endl;
}

GPUWorkerPool::~GPUWorkerPool() {
    stop();
}

void GPUWorkerPool::start() {
    for (auto& worker : workers_) {
        worker->start();
    }
}

void GPUWorkerPool::stop() {
    for (auto& worker : workers_) {
        worker->stop();
    }
}

std::future<std::string> GPUWorkerPool::submitRequest(std::shared_ptr<OCRRequest> request) {
    auto future = request->result_promise.get_future();
    
    OCRWorker* worker = getAvailableWorker();
    worker->addRequest(request);
    
    return future;
}

OCRWorker* GPUWorkerPool::getAvailableWorker() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    
    // 首先尝试找空闲的worker
    for (auto& worker : workers_) {
        if (worker->isIdle()) {
            return worker.get();
        }
    }
    
    // 如果没有空闲的，轮询分配
    int index = next_worker_index_.fetch_add(1) % workers_.size();
    return workers_[index].get();
}

int GPUWorkerPool::getOptimalWorkerCount() {
    // 根据GPU内存自动计算最优Worker数量
    // 每个Worker大约需要1.5GB GPU内存
    int max_workers = gpu_memory_per_worker_mb_ > 0 ? 
        (8000 / gpu_memory_per_worker_mb_) : 2;  // 假设8GB GPU
    
    return std::min(4, std::max(1, max_workers));  // 限制在1-4个Worker之间
}

bool GPUWorkerPool::checkGPUMemory(int num_workers) {
    // 简化版本：假设内存总是足够
    // 实际部署时可根据具体情况调整
    int required_memory_mb = num_workers * gpu_memory_per_worker_mb_;
    std::cout << "GPU Memory Required: " << required_memory_mb << "MB (assuming sufficient)" << std::endl;
    return true;
}

} // namespace PaddleOCR
