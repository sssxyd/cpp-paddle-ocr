#pragma once

#include <string>
#include <mutex>
#include <windows.h>

namespace PaddleOCR {

/**
 * @brief IPC 客户端
 */
class OCRIPCClient {
public:
    explicit OCRIPCClient(const std::string& pipe_name = "\\\\.\\pipe\\ocr_service");
    ~OCRIPCClient();
    
    bool connect(int timeout_ms = 5000);
    void disconnect();
    
    std::string recognizeImage(const std::string& image_path);
    std::string sendShutdownCommand();
    std::string getServiceStatus();
    
    bool isConnected() const { return connected_; }

private:
    std::string sendRequest(const std::string& request_json);
    
    std::string pipe_name_;
    HANDLE pipe_handle_;
    bool connected_;
    std::mutex comm_mutex_;
};

} // namespace PaddleOCR
