#include "paddle_ocr/ocr_ipc_service.h"
#include <json/json.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace PaddleOCR {

// OCRIPCService 实现
OCRIPCService::OCRIPCService(const std::string& model_dir, const std::string& pipe_name, 
                           int gpu_workers, int cpu_workers)
    : model_dir_(model_dir), pipe_name_(pipe_name),  
      gpu_workers_(gpu_workers), cpu_workers_(cpu_workers), running_(false), request_counter_(0), 
      total_requests_(0), successful_requests_(0), total_processing_time_(0.0) {
    
    
    std::cout << "OCR Service Configuration:" << std::endl;
    std::cout << "  Model Directory: " << model_dir_ << std::endl;
    std::cout << "  Pipe Name: " << pipe_name_ << std::endl;
    
    // 初始化worker
    if (gpu_workers_ > 0) {
        // 使用指定的GPU Worker数量
        gpu_worker_pool_ = std::make_unique<GPUWorkerPool>(model_dir_, gpu_workers_);
        std::cout << "  Mode: GPU (" << gpu_workers_ << " Workers)" << std::endl;
        std::cout << "  GPU Memory: " << gpu_memory_mb_ << " MB" << std::endl;
    } else {
        // 使用指定的CPU Worker数量
        cpu_worker_pool_ = std::make_unique<CPUWorkerPool>(model_dir_, cpu_workers_);
        std::cout << "  Mode: CPU (" << cpu_workers_ << " Workers)" << std::endl;
    }
}

OCRIPCService::~OCRIPCService() {
    stop();
}

bool OCRIPCService::start() {
    if (running_) return true;
    
    try {
        // 启动worker
        if (has_gpu_) {
            gpu_worker_pool_->start();
        } else {
            cpu_worker_pool_->start();
        }
        
        running_ = true;
        ipc_thread_ = std::thread(&OCRIPCService::ipcServerLoop, this);
        
        std::cout << "OCR IPC Service started successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to start OCR IPC Service: " << e.what() << std::endl;
        return false;
    }
}

void OCRIPCService::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 停止IPC线程
    if (ipc_thread_.joinable()) {
        ipc_thread_.join();
    }
    
    // 等待客户端线程结束
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_);
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
    
    // 停止worker
    if (has_gpu_) {
        gpu_worker_pool_->stop();
    } else {
        cpu_worker_pool_->stop();
    }
    
    std::cout << "OCR IPC Service stopped" << std::endl;
}

void OCRIPCService::ipcServerLoop() {
    while (running_) {
        HANDLE pipe_handle = CreateNamedPipeA(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536,  // output buffer size
            65536,  // input buffer size
            0,      // default timeout
            NULL    // default security attributes
        );
        
        if (pipe_handle == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create named pipe: " << GetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        if (ConnectNamedPipe(pipe_handle, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            // 在新线程中处理客户端连接
            {
                std::lock_guard<std::mutex> lock(client_threads_mutex_);
                client_threads_.emplace_back([this, pipe_handle]() {
                    handleClientConnection(pipe_handle);
                });
            }
        } else {
            CloseHandle(pipe_handle);
        }
    }
}

void OCRIPCService::handleClientConnection(HANDLE pipe_handle) {
    const int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    while (running_) {
        if (ReadFile(pipe_handle, buffer, BUFFER_SIZE - 1, &bytes_read, NULL)) {
            buffer[bytes_read] = '\0';
            std::string request(buffer);
            
            std::string response = processIPCRequest(request);
            
            DWORD bytes_written;
            WriteFile(pipe_handle, response.c_str(), response.length(), &bytes_written, NULL);
        } else {
            break;  // 客户端断开连接
        }
    }
    
    DisconnectNamedPipe(pipe_handle);
    CloseHandle(pipe_handle);
}

std::string OCRIPCService::processIPCRequest(const std::string& request_json) {
    try {
        Json::Value request;
        Json::CharReaderBuilder builder;
        std::istringstream stream(request_json);
        std::string errors;
        
        if (!Json::parseFromStream(builder, stream, &request, &errors)) {
            Json::Value error_response;
            error_response["success"] = false;
            error_response["error"] = "Invalid JSON: " + errors;
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, error_response);
        }
        
        std::string command = request.get("command", "").asString();
        
        if (command == "recognize") {
            std::string image_path = request.get("image_path", "").asString();
            
            if (image_path.empty()) {
                Json::Value error_response;
                error_response["success"] = false;
                error_response["error"] = "Missing image_path";
                Json::StreamWriterBuilder writer_builder;
                return Json::writeString(writer_builder, error_response);
            }
            
            auto future = processOCRRequest(image_path);
            return future.get();
        }
        else if (command == "status") {
            Json::Value status_response;
            status_response["success"] = true;
            status_response["status"] = getStatusInfo();
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, status_response);
        }
        else {
            Json::Value error_response;
            error_response["success"] = false;
            error_response["error"] = "Unknown command: " + command;
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, error_response);
        }
    }
    catch (const std::exception& e) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = e.what();
        Json::StreamWriterBuilder writer_builder;
        return Json::writeString(writer_builder, error_response);
    }
}

std::future<std::string> OCRIPCService::processOCRRequest(const std::string& image_path) {
    int request_id = request_counter_.fetch_add(1);
    auto request = std::make_shared<OCRRequest>(request_id, image_path);
    
    total_requests_.fetch_add(1);
    
    if (has_gpu_) {
        return gpu_worker_pool_->submitRequest(request);
    } else {
        return cpu_worker_pool_->submitRequest(request);
    }
}

std::future<std::string> OCRIPCService::processOCRRequest(const cv::Mat& image) {
    int request_id = request_counter_.fetch_add(1);
    auto request = std::make_shared<OCRRequest>(request_id, image);
    
    total_requests_.fetch_add(1);
    
    if (has_gpu_) {
        return gpu_worker_pool_->submitRequest(request);
    } else {
        return cpu_worker_pool_->submitRequest(request);
    }
}

std::string OCRIPCService::getStatusInfo() const {
    Json::Value status;
    status["running"] = running_.load();
    status["total_requests"] = total_requests_.load();
    status["successful_requests"] = successful_requests_.load();
    status["average_processing_time_ms"] = total_requests_.load() > 0 ? 
        total_processing_time_.load() / total_requests_.load() : 0.0;
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, status);
}

} // namespace PaddleOCR
