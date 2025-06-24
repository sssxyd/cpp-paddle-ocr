#include "paddle_ocr/ocr_ipc_client.h"
#include <json/json.h>
#include <iostream>
#include <vector>

namespace PaddleOCR {

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
    request["image_path"] = image_path;
    
    Json::StreamWriterBuilder builder;
    return sendRequest(Json::writeString(builder, request));
}

std::string OCRIPCClient::recognizeImage(const cv::Mat& image) {
    // 将图像编码为base64
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    
    // 这里简化处理，实际应该将图像数据编码为base64传输
    // 或者先保存到临时文件再传输路径
    std::string temp_path = "temp_" + std::to_string(GetTickCount()) + ".jpg";
    cv::imwrite(temp_path, image);
    
    auto result = recognizeImage(temp_path);
    
    // 删除临时文件
    DeleteFileA(temp_path.c_str());
    
    return result;
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
