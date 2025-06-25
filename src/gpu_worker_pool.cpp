#include "paddle_ocr/gpu_worker_pool.h"
#include <iostream>
#include <algorithm>

namespace PaddleOCR {

// GPUWorkerPool 实现
GPUWorkerPool::GPUWorkerPool(const std::string& model_dir, int num_workers) 
    : next_worker_index_(0) {
        
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

} // namespace PaddleOCR
