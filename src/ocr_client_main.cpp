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
    std::wcout << L"OCR IPC Client 1.0.1\n";
    std::wcout << L"Usage: ocr_client [options] <image_path>\n";
    std::wcout << L"\nOptions:\n";
    std::wcout << L"  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::wcout << L"  --timeout <ms>        连接超时时间 (默认: 5000ms)\n";
    std::wcout << L"  --status              获取服务状态信息\n";
    std::wcout << L"  --shutdown            优雅关闭OCR服务\n";
    std::wcout << L"  --help                显示此帮助信息\n";
    std::wcout << L"\n示例:\n";
    std::wcout << L"  ocr_client image.jpg\n";
    std::wcout << L"  ocr_client --status\n";
    std::wcout << L"  ocr_client --shutdown\n";
    std::wcout << L"  ocr_client --pipe-name \\\\.\\pipe\\ocr_service image.jpg\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    setupConsole();
#endif

    std::string pipe_name = "\\\\.\\pipe\\ocr_service";
    std::string image_path;
    int timeout_ms = 5000;
    bool get_status = false;
    bool shutdown_service = false;
    
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
        else if (arg[0] != '-') {
            image_path = arg;
        }else {
            std::wcerr << L"Unknown argument: " << std::wstring(arg.begin(), arg.end()) << std::endl;
            printUsage();
            return 1;
        }
    }    
    if (!get_status && !shutdown_service && image_path.empty()) {
        std::wcerr << L"Error: Image path is required" << std::endl;
        printUsage();
        return 1;
    }
    
    try {
        // 处理关闭服务的请求
        if (shutdown_service) {
            // 通过管道发送shutdown命令
            PaddleOCR::OCRIPCClient client(pipe_name);
            std::wcout << L"连接到OCR服务以发送关闭命令..." << std::endl;
            
            if (!client.connect(timeout_ms)) {
                std::wcerr << L"无法连接到OCR服务。服务可能没有运行。" << std::endl;
                return 1;
            }
            
            std::wcout << L"发送关闭命令..." << std::endl;
            
            try {
                std::string response = client.sendShutdownCommand();
                std::wcout << L"收到响应，长度: " << response.length() << std::endl;                                
                std::wcout << L"关闭命令处理完成。" << std::endl;
                
            } catch (const std::exception& e) {
                // 显示具体的异常信息
                std::wstring error_msg = utf8ToWideString(e.what());
                std::wcout << L"发送关闭命令时发生异常: " << error_msg << std::endl;
                std::wcout << L"这可能是正常的，因为服务正在关闭..." << std::endl;
            } catch (...) {
                std::wcout << L"发送关闭命令时发生未知异常，服务可能正在关闭..." << std::endl;
            }
            
            client.disconnect();
            return 0;
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
            std::wcout << L"获取服务状态信息..." << std::endl;
            
            try {
                std::string response = client.getServiceStatus();
                
                // 解析响应
                Json::Value json_response;
                Json::CharReaderBuilder reader_builder;
                std::istringstream stream(response);
                std::string errors;
                
                if (Json::parseFromStream(reader_builder, stream, &json_response, &errors)) {
                    if (json_response["success"].asBool()) {
                        std::wcout << L"\n=== 服务状态信息 ===" << std::endl;
                        std::wstring status_info = utf8ToWideString(json_response["status"].asString());
                        std::wcout << status_info << std::endl;
                    } else {
                        std::wstring error_msg = utf8ToWideString(json_response["error"].asString());
                        std::wcerr << L"获取状态失败: " << error_msg << std::endl;
                        return 1;
                    }
                } else {
                    std::wstring error_wide = utf8ToWideString(errors);
                    std::wcerr << L"解析状态响应失败: " << error_wide << std::endl;
                    return 1;
                }
            } catch (const std::exception& e) {
                std::wstring error_msg = utf8ToWideString(e.what());
                std::wcerr << L"获取状态时发生错误: " << error_msg << std::endl;
                return 1;
            }
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
