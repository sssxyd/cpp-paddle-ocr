#include "paddle_ocr/ocr_ipc_client.h"
#include <json/json.h>
#include <iostream>
#include <vector>
#include <filesystem>

namespace PaddleOCR {

// Base64编码辅助函数
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uchar>& data) {
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

static std::string matToBase64(const cv::Mat& image) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    return base64Encode(buffer);
}

static size_t getFileSize(const std::string& filepath) {
    try {
        return std::filesystem::file_size(filepath);
    } catch (const std::exception&) {
        return 0;
    }
}

// OCRIPCClient 实现
OCRIPCClient::OCRIPCClient(const std::string& pipe_name) 
    : pipe_name_(pipe_name), pipe_handle_(INVALID_HANDLE_VALUE), connected_(false) {
}

OCRIPCClient::~OCRIPCClient() {
    disconnect();
}

bool OCRIPCClient::connect(int timeout_ms) {
    if (connected_) return true;
    
    DWORD start_time = GetTickCount();
    
    while (GetTickCount() - start_time < timeout_ms) {
        pipe_handle_ = CreateFileA(
            pipe_name_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            connected_ = true;
            return true;
        }
        
        if (GetLastError() != ERROR_PIPE_BUSY) {
            break;
        }
        
        if (!WaitNamedPipeA(pipe_name_.c_str(), 1000)) {
            break;
        }
    }
    
    return false;
}

void OCRIPCClient::disconnect() {
    if (connected_) {
        CloseHandle(pipe_handle_);
        pipe_handle_ = INVALID_HANDLE_VALUE;
        connected_ = false;
    }
}

std::string OCRIPCClient::recognizeImage(const std::string& image_path) {
    Json::Value request;
    request["command"] = "recognize";
      // 根据文件大小智能选择传输方式
    size_t file_size = getFileSize(image_path);
    // 考虑Base64编码开销(+33%)和JSON开销，限制在600KB以内确保能放入1MB缓冲区
    const size_t THRESHOLD = 600 * 1024; // 600KB
      if (file_size > 0 && file_size < THRESHOLD) {
        // 小文件：读取并编码为Base64传输
        cv::Mat image = cv::imread(image_path);
        if (!image.empty()) {
            std::string base64_data = matToBase64(image);
            
            // 双重检查：验证最终JSON大小
            Json::Value temp_request;
            temp_request["command"] = "recognize";
            temp_request["image_data"] = base64_data;
            Json::StreamWriterBuilder builder;
            std::string json_str = Json::writeString(builder, temp_request);
            
            if (json_str.length() < 1000000) {  // 小于1MB
                request["image_data"] = base64_data;
                std::cout << "Using Base64 transmission (JSON size: " 
                         << json_str.length() << " bytes)" << std::endl;
            } else {
                // Base64数据太大，回退到路径传输
                request["image_path"] = image_path;
                std::cout << "Base64 too large (" << json_str.length() 
                         << " bytes), using path transmission" << std::endl;
            }
        } else {
            // 如果无法读取图像，回退到路径传输
            request["image_path"] = image_path;
        }
    } else {
        // 大文件或无法获取文件大小：使用路径传输
        request["image_path"] = image_path;
        std::cout << "Using path transmission (file size: " << file_size << " bytes)" << std::endl;
    }
    
    Json::StreamWriterBuilder builder;
    return sendRequest(Json::writeString(builder, request));
}

std::string OCRIPCClient::recognizeImage(const cv::Mat& image) {
    Json::Value request;
    request["command"] = "recognize";
    
    // 对于cv::Mat，编码为Base64并检查大小
    std::string base64_data = matToBase64(image);
    
    // 检查最终JSON大小
    Json::Value temp_request;
    temp_request["command"] = "recognize";
    temp_request["image_data"] = base64_data;
    Json::StreamWriterBuilder builder;
    std::string json_str = Json::writeString(builder, temp_request);
    
    if (json_str.length() < 1000000) {  // 小于1MB
        request["image_data"] = base64_data;
        std::cout << "Using Base64 transmission (JSON size: " 
                 << json_str.length() << " bytes)" << std::endl;
    } else {
        // 数据太大，保存为临时文件后使用路径传输
        std::string temp_path = "temp_" + std::to_string(GetTickCount()) + ".jpg";
        cv::imwrite(temp_path, image);
        request["image_path"] = temp_path;
        std::cout << "Base64 too large (" << json_str.length() 
                 << " bytes), using temporary file: " << temp_path << std::endl;
        
        // 注意：这里创建了临时文件，调用者需要负责清理
        // 在实际应用中，可能需要更好的临时文件管理策略
    }
    
    Json::StreamWriterBuilder final_builder;
    return sendRequest(Json::writeString(final_builder, request));
}

std::string OCRIPCClient::sendRequest(const std::string& request_json) {
    if (!connected_) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Not connected to service";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
    
    std::lock_guard<std::mutex> lock(comm_mutex_);
    
    // 发送请求
    DWORD bytes_written;
    if (!WriteFile(pipe_handle_, request_json.c_str(), request_json.length(), &bytes_written, NULL)) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to send request";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
    
    // 读取响应
    const int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    if (ReadFile(pipe_handle_, buffer, BUFFER_SIZE - 1, &bytes_read, NULL)) {
        buffer[bytes_read] = '\0';
        return std::string(buffer);
    } else {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to read response";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
}

} // namespace PaddleOCR
