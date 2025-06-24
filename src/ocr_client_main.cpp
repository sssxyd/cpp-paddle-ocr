#include "paddle_ocr/ocr_service.h"
#include <iostream>
#include <chrono>
#include <json/json.h>

void printUsage() {
    std::cout << "OCR IPC Client\n";
    std::cout << "Usage: ocr_client [options] <image_path>\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::cout << "  --timeout <ms>        连接超时时间 (默认: 5000ms)\n";
    std::cout << "  --status              获取服务状态信息\n";
    std::cout << "  --help                显示此帮助信息\n";
    std::cout << "\n示例:\n";
    std::cout << "  ocr_client image.jpg\n";
    std::cout << "  ocr_client --status\n";
    std::cout << "  ocr_client --pipe-name \\\\.\\pipe\\my_ocr image.jpg\n";
}

int main(int argc, char* argv[]) {
    std::string pipe_name = "\\\\.\\pipe\\ocr_service";
    std::string image_path;
    int timeout_ms = 5000;
    bool get_status = false;
    
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
        }
        else if (arg == "--status") {
            get_status = true;
        }
        else if (arg[0] != '-') {
            image_path = arg;
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return 1;
        }
    }
    
    if (!get_status && image_path.empty()) {
        std::cerr << "Error: Image path is required" << std::endl;
        printUsage();
        return 1;
    }
    
    try {
        PaddleOCR::OCRIPCClient client(pipe_name);
        
        std::cout << "Connecting to OCR service..." << std::endl;
        if (!client.connect(timeout_ms)) {
            std::cerr << "Failed to connect to OCR service. Is the service running?" << std::endl;
            return 1;
        }
        
        std::cout << "Connected successfully!" << std::endl;
        
        if (get_status) {
            // 获取状态信息
            Json::Value request;
            request["command"] = "status";
            
            Json::StreamWriterBuilder builder;
            std::string request_str = Json::writeString(builder, request);
            
            // 这里需要直接调用内部方法，实际应该扩展客户端API
            std::cout << "Service status information:" << std::endl;
            // 简化处理，在实际实现中应该添加getStatus方法
        } else {
            // 执行OCR识别
            auto start_time = std::chrono::high_resolution_clock::now();
            
            std::cout << "Processing image: " << image_path << std::endl;
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
                    std::cout << "\n=== OCR Results ===" << std::endl;
                    std::cout << "Processing Time: " << json_result["processing_time_ms"].asDouble() << " ms" << std::endl;
                    std::cout << "Worker ID: " << json_result["worker_id"].asInt() << std::endl;
                    std::cout << "Total Time: " << duration.count() << " ms" << std::endl;
                    
                    const Json::Value& texts = json_result["texts"];
                    if (texts.isArray() && !texts.empty()) {
                        std::cout << "\nDetected Texts:" << std::endl;
                        for (Json::ArrayIndex i = 0; i < texts.size(); ++i) {
                            std::cout << "  [" << i << "] " << texts[i].asString() << std::endl;
                        }
                        
                        const Json::Value& boxes = json_result["boxes"];
                        if (boxes.isArray() && boxes.size() == texts.size()) {
                            std::cout << "\nBounding Boxes:" << std::endl;
                            for (Json::ArrayIndex i = 0; i < boxes.size(); ++i) {
                                std::cout << "  [" << i << "] ";
                                const Json::Value& box = boxes[i];
                                for (Json::ArrayIndex j = 0; j < box.size(); ++j) {
                                    const Json::Value& point = box[j];
                                    std::cout << "(" << point[0].asInt() << "," << point[1].asInt() << ")";
                                    if (j < box.size() - 1) std::cout << " ";
                                }
                                std::cout << std::endl;
                            }
                        }
                    } else {
                        std::cout << "No text detected in the image." << std::endl;
                    }
                } else {
                    std::cerr << "OCR failed: " << json_result["error"].asString() << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Failed to parse response: " << errors << std::endl;
                std::cerr << "Raw response: " << result << std::endl;
                return 1;
            }
        }
        
        client.disconnect();
        std::cout << "\nDisconnected from service." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
