#include "paddle_ocr/ocr_ipc_service.h"
#include <json/json.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <future>
#include <chrono>
#include <thread>
#include <windows.h>
#include <libbase64.h>

namespace PaddleOCR {

std::vector<uchar> OCRIPCService::base64Decode(const std::string& encoded) {
    if (encoded.empty()) {
        return {};
    }
    
    // 计算解码后的最大长度 (大约是输入长度的 3/4)
    size_t max_out_len = (encoded.size() * 3) / 4 + 1;
    
    // 分配输出缓冲区
    std::vector<uchar> decoded(max_out_len);
    
    // 执行解码
    size_t actual_out_len = 0;
    int result = base64_decode(encoded.c_str(), encoded.size(), 
                              reinterpret_cast<char*>(decoded.data()), &actual_out_len, 0);
    
    if (result == 1) {  // 成功
        decoded.resize(actual_out_len);
        return decoded;
    }
    
    return {}; // 解码失败
}

cv::Mat OCRIPCService::base64ToMat(const std::string& base64_string) {
    std::vector<uchar> data = base64Decode(base64_string);
    return cv::imdecode(data, cv::IMREAD_COLOR);
}

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
        if (gpu_workers_ > 0) {
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
    if (gpu_workers_ > 0) {
        gpu_worker_pool_->stop();
    } else {
        cpu_worker_pool_->stop();
    }
    
    std::cout << "OCR IPC Service stopped" << std::endl;
}

void OCRIPCService::ipcServerLoop() {
    std::cout << "OCR IPC Server started, waiting for clients..." << std::endl;
    
    auto last_cleanup = std::chrono::steady_clock::now();
    const auto cleanup_interval = std::chrono::seconds(30);  // 每30秒清理一次完成的线程
    
    while (running_) {
        // 周期性清理已完成的客户端线程
        auto now = std::chrono::steady_clock::now();
        if (now - last_cleanup >= cleanup_interval) {
            cleanupFinishedClientThreads();
            last_cleanup = now;
        }
        
        // 1. 创建命名管道实例
        HANDLE pipe_handle = CreateNamedPipeA(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            PIPE_OUTPUT_BUFFER_SIZE,  // 使用类常量：64KB
            PIPE_INPUT_BUFFER_SIZE,   // 使用类常量：1MB
            0,      // default timeout
            NULL    // default security attributes
        );
        
        if (pipe_handle == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create named pipe: " << GetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        // 2. 等待客户端连接
        if (ConnectNamedPipe(pipe_handle, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            // 3. 在新线程中处理客户端连接
            {
                std::lock_guard<std::mutex> lock(client_threads_mutex_);
                client_threads_.emplace_back([this, pipe_handle]() {
                    handleClientConnection(pipe_handle);
                });
                std::cout << "New client connected. Active client threads: " << client_threads_.size() << std::endl;
            }
        } else {
            CloseHandle(pipe_handle);
        }
    }
    
    std::cout << "IPC Server loop exiting..." << std::endl;
}

void OCRIPCService::cleanupFinishedClientThreads() {
    std::lock_guard<std::mutex> lock(client_threads_mutex_);
    
    // 使用 try_join 模拟的方式，实际上我们需要维护线程状态
    // 更安全的方式是使用 std::future 或状态标志
    // 这里先简化处理：移除不可join的线程
    
    auto it = client_threads_.begin();
    size_t initial_count = client_threads_.size();
    
    while (it != client_threads_.end()) {
        if (!it->joinable()) {
            // 线程已结束且资源已释放
            it = client_threads_.erase(it);
        } else {
            ++it;
        }
    }
    
    size_t cleaned_count = initial_count - client_threads_.size();
    if (cleaned_count > 0) {
        std::cout << "Cleaned up " << cleaned_count << " finished client threads. "
                  << "Active threads: " << client_threads_.size() << std::endl;
    }
}

void OCRIPCService::handleClientConnection(HANDLE pipe_handle) {
    char* buffer = new char[READ_BUFFER_SIZE];  // 使用类常量：1MB，动态分配避免栈溢出
    DWORD bytes_read;
    DWORD client_thread_id = GetCurrentThreadId();
    
    std::cout << "[Thread-" << client_thread_id << "] Client connected, starting message loop..." << std::endl;
    
    while (running_) {
        if (ReadFile(pipe_handle, buffer, READ_BUFFER_SIZE - 1, &bytes_read, NULL)) {
            if (bytes_read == 0) {
                // 客户端发送了空数据或准备关闭
                std::cout << "[Thread-" << client_thread_id << "] Received 0 bytes, client may be closing..." << std::endl;
                continue;
            }
            
            buffer[bytes_read] = '\0';
            std::cout << "[Thread-" << client_thread_id << "] Received " << bytes_read << " bytes from client" << std::endl;
            
            // 检查数据是否可能被截断
            if (bytes_read == READ_BUFFER_SIZE - 1) {
                std::cerr << "[Thread-" << client_thread_id << "] Warning: Received data may be truncated (reached buffer limit of " 
                         << READ_BUFFER_SIZE << " bytes)" << std::endl;
                
                // 返回错误响应
                Json::Value error_response;
                error_response["success"] = false;
                error_response["error"] = "Data too large for buffer (max 1MB). Consider using file path transmission.";
                Json::StreamWriterBuilder writer_builder;
                std::string error_str = Json::writeString(writer_builder, error_response);
                
                DWORD bytes_written;
                if (!WriteFile(pipe_handle, error_str.c_str(), error_str.length(), &bytes_written, NULL)) {
                    std::cerr << "[Thread-" << client_thread_id << "] Failed to send error response: " << GetLastError() << std::endl;
                    break;
                }
                continue;
            }
            
            std::string request(buffer);
            std::string response = processIPCRequest(request);
            
            DWORD bytes_written;
            if (!WriteFile(pipe_handle, response.c_str(), response.length(), &bytes_written, NULL)) {
                DWORD error = GetLastError();
                std::cerr << "[Thread-" << client_thread_id << "] Failed to send response: " << error << std::endl;
                if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                    std::cout << "[Thread-" << client_thread_id << "] Client disconnected during write" << std::endl;
                }
                break;
            }
            
            std::cout << "[Thread-" << client_thread_id << "] Sent " << bytes_written << " bytes response to client" << std::endl;
        } else {
            // ReadFile 失败，检查具体原因
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                std::cout << "[Thread-" << client_thread_id << "] Client disconnected (broken pipe)" << std::endl;
            } else if (error == ERROR_NO_DATA) {
                std::cout << "[Thread-" << client_thread_id << "] Client closed connection (no data)" << std::endl;
            } else {
                std::cerr << "[Thread-" << client_thread_id << "] ReadFile failed with error: " << error << std::endl;
            }
            break;  // 客户端断开连接或发生错误
        }
    }
    
    std::cout << "[Thread-" << client_thread_id << "] Cleaning up client connection..." << std::endl;
    
    delete[] buffer;
    
    // 断开命名管道连接
    if (!DisconnectNamedPipe(pipe_handle)) {
        DWORD error = GetLastError();
        if (error != ERROR_INVALID_HANDLE) {  // 如果句柄已无效，不需要报错
            std::cerr << "[Thread-" << client_thread_id << "] DisconnectNamedPipe failed: " << error << std::endl;
        }
    }
    
    // 关闭管道句柄
    if (!CloseHandle(pipe_handle)) {
        std::cerr << "[Thread-" << client_thread_id << "] CloseHandle failed: " << GetLastError() << std::endl;
    }
    
    std::cout << "[Thread-" << client_thread_id << "] Client connection cleanup completed" << std::endl;
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
            cv::Mat image;
            std::string error_msg;
            
            // 检查传输方式：路径、Base64数据或字节数组
            std::string image_path = request.get("image_path", "").asString();
            std::string image_base64 = request.get("image_data", "").asString();
            
            if (!image_path.empty()) {
                // 方式1: 使用文件路径
                image = cv::imread(image_path);
                if (image.empty()) {
                    error_msg = "Failed to load image from path: " + image_path;
                }
            }
            else if (!image_base64.empty()) {
                // 方式2: 使用Base64编码数据
                try {
                    image = base64ToMat(image_base64);
                    if (image.empty()) {
                        error_msg = "Failed to decode base64 image data";
                    }
                } catch (const std::exception& e) {
                    error_msg = "Base64 decode error: " + std::string(e.what());
                }
            }
            else {
                error_msg = "Missing image_path or image_data";
            }
            
            // 如果有错误，返回错误响应
            if (!error_msg.empty()) {
                Json::Value error_response;
                error_response["success"] = false;
                error_response["error"] = error_msg;
                Json::StreamWriterBuilder writer_builder;
                return Json::writeString(writer_builder, error_response);
            }
            
            // 统一处理cv::Mat格式的图像
            auto future = processOCRRequest(image);
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

std::future<std::string> OCRIPCService::processOCRRequest(const cv::Mat& image) {
    int request_id = request_counter_.fetch_add(1);
    auto request = std::make_shared<OCRRequest>(request_id, image);
    
    total_requests_.fetch_add(1);
    
    if (gpu_workers_ > 0) {
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
