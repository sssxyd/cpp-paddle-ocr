#include "paddle_ocr/ocr_ipc_client.h"
#include <json/json.h>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <libbase64.h>

namespace PaddleOCR {

static std::string base64Encode(const std::vector<unsigned char>& data) {
    if (data.empty()) {
        return "";
    }
    
    // 计算编码后的最大长度 (大约是输入长度的 4/3，向上取整并加上填充)
    size_t max_out_len = ((data.size() + 2) / 3) * 4 + 1; // +1 for null terminator safety
    
    // 分配输出缓冲区
    std::string encoded(max_out_len, '\0');
    
    // 执行编码
    size_t actual_out_len = 0;
    base64_encode(reinterpret_cast<const char*>(data.data()), data.size(), 
                  &encoded[0], &actual_out_len, 0);
    
    // 调整字符串大小到实际长度
    encoded.resize(actual_out_len);
    return encoded;
}

static size_t getFileSize(const std::string& filepath) {
    try {
        return std::filesystem::file_size(filepath);
    } catch (const std::exception&) {
        return 0;
    }
}

// 将图片文件读取为字节数组
static std::vector<unsigned char> readFileToBytes(const std::string& image_path) {
    std::vector<unsigned char> buffer;
    
    try {
        // 打开文件（二进制模式）
        std::ifstream file(image_path, std::ios::binary | std::ios::ate);
        
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file: " << image_path << std::endl;
            return buffer; // 返回空的vector
        }
        
        // 获取文件大小
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (file_size <= 0) {
            std::cerr << "Error: Invalid file size: " << file_size << std::endl;
            return buffer;
        }
        
        // 调整vector大小并读取整个文件
        buffer.resize(static_cast<size_t>(file_size));
        
        if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
            std::cerr << "Error: Failed to read file: " << image_path << std::endl;
            buffer.clear(); // 读取失败，清空buffer
            return buffer;
        }
        
        file.close();
        std::cout << "Successfully read " << file_size << " bytes from: " << image_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception while reading file: " << e.what() << std::endl;
        buffer.clear();
    }
    
    return buffer;
}

// 直接将图片文件转换为Base64（不使用OpenCV）
static std::string fileToBase64(const std::string& image_path) {
    // 读取文件为字节数组
    std::vector<unsigned char> file_bytes = readFileToBytes(image_path);
    
    if (file_bytes.empty()) {
        return ""; // 读取失败
    }
    
    // 转换为Base64
    return base64Encode(file_bytes);
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
        std::string base64_data = fileToBase64(image_path);
        if (!base64_data.empty()) {
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
        DWORD error = GetLastError();
        std::cerr << "WriteFile failed with error: " << error << std::endl;
        
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to send request (error " + std::to_string(error) + ")";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
    
    std::cout << "Sent " << bytes_written << " bytes to server" << std::endl;
    
    // 读取响应
    const int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    if (ReadFile(pipe_handle_, buffer, BUFFER_SIZE - 1, &bytes_read, NULL)) {
        buffer[bytes_read] = '\0';
        std::cout << "Received " << bytes_read << " bytes from server" << std::endl;
        return std::string(buffer);
    } else {
        DWORD error = GetLastError();
        std::cerr << "ReadFile failed with error: " << error << std::endl;
        
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to read response (error " + std::to_string(error) + ")";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
}

std::string OCRIPCClient::sendShutdownCommand() {
    Json::Value request;
    request["command"] = "shutdown";
    
    Json::StreamWriterBuilder builder;
    std::string request_json = Json::writeString(builder, request);
    
    return sendRequest(request_json);
}

std::string OCRIPCClient::getServiceStatus() {
    Json::Value request;
    request["command"] = "status";
    
    Json::StreamWriterBuilder builder;
    std::string request_json = Json::writeString(builder, request);
    
    return sendRequest(request_json);
}

} // namespace PaddleOCR
