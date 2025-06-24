#include "paddle_ocr/cpu_worker_pool.h"
#include <iostream>

namespace PaddleOCR {

// CPUWorkerPool 实现
CPUWorkerPool::CPUWorkerPool(const std::string& model_dir, int num_workers) 
    : next_worker_index_(0) {
    
    workers_.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(std::make_unique<OCRWorker>(i, model_dir, false));
    }
    
    std::cout << "CPUWorkerPool created with " << num_workers << " workers" << std::endl;
}

CPUWorkerPool::~CPUWorkerPool() {
    stop();
}

void CPUWorkerPool::start() {
    for (auto& worker : workers_) {
        worker->start();
    }
}

void CPUWorkerPool::stop() {
    for (auto& worker : workers_) {
        worker->stop();
    }
}

std::future<std::string> CPUWorkerPool::submitRequest(std::shared_ptr<OCRRequest> request) {
    auto future = request->result_promise.get_future();
    
    OCRWorker* worker = getAvailableWorker();
    worker->addRequest(request);
    
    return future;
}

OCRWorker* CPUWorkerPool::getAvailableWorker() {
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
