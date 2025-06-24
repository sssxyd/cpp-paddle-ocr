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

namespace PaddleOCR {

// Base64编码表
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 编码/解码辅助函数实现
std::string OCRIPCService::base64Encode(const std::vector<uchar>& data) {
    std::string encoded;
    int val = 0, valb = -6;
    for (uchar c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4) encoded.push_back('=');
    return encoded;
}

std::vector<uchar> OCRIPCService::base64Decode(const std::string& encoded) {
    std::vector<uchar> decoded;
    int val = 0, valb = -8;
    for (uchar c : encoded) {
        if (c == '=') break;
        if (base64_chars.find(c) == std::string::npos) continue;
        val = (val << 6) + base64_chars.find(c);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return decoded;
}

cv::Mat OCRIPCService::base64ToMat(const std::string& base64_string) {
    std::vector<uchar> data = base64Decode(base64_string);
    return cv::imdecode(data, cv::IMREAD_COLOR);
}

std::string OCRIPCService::matToBase64(const cv::Mat& image) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    return base64Encode(buffer);
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
    while (running_) {
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
    char* buffer = new char[READ_BUFFER_SIZE];  // 使用类常量：1MB，动态分配避免栈溢出
    DWORD bytes_read;
    
    while (running_) {
        if (ReadFile(pipe_handle, buffer, READ_BUFFER_SIZE - 1, &bytes_read, NULL)) {
            buffer[bytes_read] = '\0';
            
            // 检查数据是否可能被截断
            if (bytes_read == READ_BUFFER_SIZE - 1) {
                std::cerr << "Warning: Received data may be truncated (reached buffer limit of " 
                         << READ_BUFFER_SIZE << " bytes)" << std::endl;
                
                // 返回错误响应
                Json::Value error_response;
                error_response["success"] = false;
                error_response["error"] = "Data too large for buffer (max 1MB). Consider using file path transmission.";
                Json::StreamWriterBuilder writer_builder;
                std::string error_str = Json::writeString(writer_builder, error_response);
                
                DWORD bytes_written;
                WriteFile(pipe_handle, error_str.c_str(), error_str.length(), &bytes_written, NULL);
                continue;
            }
            
            std::string request(buffer);
            std::string response = processIPCRequest(request);
            
            DWORD bytes_written;
            WriteFile(pipe_handle, response.c_str(), response.length(), &bytes_written, NULL);
        } else {
            break;  // 客户端断开连接
        }
    }
    
    delete[] buffer;
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
