#include <paddle_ocr/ocr_ipc_client.h>
#include <iostream>
#include <chrono>
#include <json/json.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <locale>
#endif

#ifdef _WIN32
std::wstring utf8ToWideString(const std::string& utf8_str) {
    if (utf8_str.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void setupConsole() {
    // 设置控制台代码页为UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // 设置控制台模式以支持Unicode
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    // 设置locale为UTF-8
    std::locale::global(std::locale(""));
}
#endif

void printUsage() {
    std::wcout << L"OCR IPC Client\n";
    std::wcout << L"Usage: ocr_client [options] <image_path>\n";
    std::wcout << L"\nOptions:\n";
    std::wcout << L"  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::wcout << L"  --timeout <ms>        连接超时时间 (默认: 5000ms)\n";
    std::wcout << L"  --status              获取服务状态信息\n";
    std::wcout << L"  --shutdown            优雅关闭OCR服务\n";
    std::wcout << L"  --shutdown-event <name>  通过指定事件名称关闭服务\n";
    std::wcout << L"  --help                显示此帮助信息\n";
    std::wcout << L"\n示例:\n";
    std::wcout << L"  ocr_client image.jpg\n";
    std::wcout << L"  ocr_client --status\n";
    std::wcout << L"  ocr_client --shutdown\n";
    std::wcout << L"  ocr_client --shutdown-event Global\\\\OCRServiceShutdown\n";
    std::wcout << L"  ocr_client --pipe-name \\\\.\\pipe\\my_ocr image.jpg\n";
}

// 通过命名事件关闭服务
bool shutdownServiceViaEvent(const std::string& event_name) {
    std::wcout << L"尝试通过事件关闭服务: " << std::wstring(event_name.begin(), event_name.end()) << std::endl;
    
    // 尝试打开现有的事件
    HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
    if (hEvent == NULL) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            std::wcerr << L"错误: 找不到指定的事件。服务可能没有使用该事件名称启动。" << std::endl;
        } else {
            std::wcerr << L"错误: 无法打开事件，错误代码: " << error << std::endl;
        }
        return false;
    }
    
    // 设置事件以通知服务关闭
    if (SetEvent(hEvent)) {
        std::wcout << L"关闭事件已发送，服务应该会优雅关闭..." << std::endl;
        CloseHandle(hEvent);
        return true;
    } else {
        DWORD error = GetLastError();
        std::wcerr << L"错误: 无法设置事件，错误代码: " << error << std::endl;
        CloseHandle(hEvent);
        return false;
    }
}

// 自动发现并关闭服务
bool shutdownServiceAuto(const std::string& pipe_name, int timeout_ms) {
    // 方法1: 尝试通过默认事件名称关闭
    std::string default_event_name = "Global\\OCRServiceShutdown";
    std::wcout << L"尝试方法1: 通过默认事件名称关闭..." << std::endl;
    if (shutdownServiceViaEvent(default_event_name)) {
        return true;
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    setupConsole();
#endif

    std::string pipe_name = "\\\\.\\pipe\\ocr_service";
    std::string image_path;
    std::string shutdown_event_name = "";
    int timeout_ms = 5000;
    bool get_status = false;
    bool shutdown_service = false;
    bool shutdown_via_event = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        else if (arg == "--pipe-name" && i + 1 < argc) {
            pipe_name = argv[++i];
        }
        else if (arg == "--timeout" && i + 1 < argc) {
            timeout_ms = std::stoi(argv[++i]);
        }        else if (arg == "--status") {
            get_status = true;
        }
        else if (arg == "--shutdown") {
            shutdown_service = true;
        }
        else if (arg == "--shutdown-event" && i + 1 < argc) {
            shutdown_event_name = argv[++i];
            shutdown_via_event = true;
        }
        else if (arg[0] != '-') {
            image_path = arg;
        }else {
            std::wcerr << L"Unknown argument: " << std::wstring(arg.begin(), arg.end()) << std::endl;
            printUsage();
            return 1;
        }
    }    
    if (!get_status && !shutdown_service && !shutdown_via_event && image_path.empty()) {
        std::wcerr << L"Error: Image path is required" << std::endl;
        printUsage();
        return 1;    
    }
    
    try {
        // 处理关闭服务的请求
        if (shutdown_via_event) {
            // 通过指定的事件名称关闭服务
            return shutdownServiceViaEvent(shutdown_event_name) ? 0 : 1;
        }
        else if (shutdown_service) {
            // 自动发现并关闭服务
            return shutdownServiceAuto(pipe_name, timeout_ms) ? 0 : 1;
        }
        
        // 正常的OCR操作需要连接到服务
        PaddleOCR::OCRIPCClient client(pipe_name);
          std::wcout << L"Connecting to OCR service..." << std::endl;
        if (!client.connect(timeout_ms)) {
            std::wcerr << L"Failed to connect to OCR service. Is the service running?" << std::endl;
            return 1;
        }
        
        std::wcout << L"Connected successfully!" << std::endl;
        
        if (get_status) {
            // 获取状态信息
            Json::Value request;
            request["command"] = "status";
            
            Json::StreamWriterBuilder builder;
            std::string request_str = Json::writeString(builder, request);
            
            // 这里需要直接调用内部方法，实际应该扩展客户端API
            std::wcout << L"Service status information:" << std::endl;
            // 简化处理，在实际实现中应该添加getStatus方法
        } else {            // 执行OCR识别
            auto start_time = std::chrono::high_resolution_clock::now();
            
            std::wcout << L"Processing image: " << std::wstring(image_path.begin(), image_path.end()) << std::endl;
            std::string result = client.recognizeImage(image_path);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<double, std::milli>(end_time - start_time);
            
            // 解析结果
            Json::Value json_result;
            Json::CharReaderBuilder reader_builder;
            std::istringstream stream(result);
            std::string errors;
              if (Json::parseFromStream(reader_builder, stream, &json_result, &errors)) {
                if (json_result["success"].asBool()) {
                    std::wcout << L"\n=== OCR Results ===" << std::endl;
                    std::wcout << L"Processing Time: " << json_result["processing_time_ms"].asDouble() << L" ms" << std::endl;
                    std::wcout << L"Worker ID: " << json_result["worker_id"].asInt() << std::endl;
                    std::wcout << L"Total Time: " << duration.count() << L" ms" << std::endl;
                    
                    const Json::Value& texts = json_result["texts"];
                    if (texts.isArray() && !texts.empty()) {
                        std::wcout << L"\nDetected Texts:" << std::endl;
                        for (Json::ArrayIndex i = 0; i < texts.size(); ++i) {
                            std::string text_utf8 = texts[i].asString();
                            std::wstring text_wide = utf8ToWideString(text_utf8);
                            std::wcout << L"  [" << i << L"] " << text_wide << std::endl;
                        }
                        
                        const Json::Value& boxes = json_result["boxes"];
                        if (boxes.isArray() && boxes.size() == texts.size()) {
                            std::wcout << L"\nBounding Boxes:" << std::endl;
                            for (Json::ArrayIndex i = 0; i < boxes.size(); ++i) {
                                std::wcout << L"  [" << i << L"] ";
                                const Json::Value& box = boxes[i];
                                for (Json::ArrayIndex j = 0; j < box.size(); ++j) {
                                    const Json::Value& point = box[j];
                                    std::wcout << L"(" << point[0].asInt() << L"," << point[1].asInt() << L")";
                                    if (j < box.size() - 1) std::wcout << L" ";
                                }
                                std::wcout << std::endl;
                            }
                        }
                    } else {
                        std::wcout << L"No text detected in the image." << std::endl;
                    }
                } else {
                    std::wstring error_msg = utf8ToWideString(json_result["error"].asString());
                    std::wcerr << L"OCR failed: " << error_msg << std::endl;
                    return 1;
                }
            } else {
                std::wstring error_wide = utf8ToWideString(errors);
                std::wstring result_wide = utf8ToWideString(result);
                std::wcerr << L"Failed to parse response: " << error_wide << std::endl;
                std::wcerr << L"Raw response: " << result_wide << std::endl;
                return 1;
            }
        }
          client.disconnect();
        std::wcout << L"\nDisconnected from service." << std::endl;
    }
    catch (const std::exception& e) {
        std::wstring error_msg = utf8ToWideString(e.what());
        std::wcerr << L"Client error: " << error_msg << std::endl;
        return 1;
    }
    
    return 0;
}
