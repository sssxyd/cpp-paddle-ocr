#include "paddle_ocr/ocr_service.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <windows.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <locale>
#endif

std::unique_ptr<PaddleOCR::OCRIPCService> g_service;

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

// 信号处理函数
BOOL WINAPI ConsoleHandler(DWORD dwType) {
    switch (dwType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::wcout << L"\nReceived shutdown signal, stopping service..." << std::endl;
        if (g_service) {
            g_service->stop();
        }
        return TRUE;
    default:
        return FALSE;
    }
}

void printUsage() {
    std::wcout << L"OCR IPC Service 1.0.1\n";
    std::wcout << L"Usage: ocr_service [options]\n";
    std::wcout << L"\nOptions:\n";
    std::wcout << L"  --model-dir <path>    模型文件目录路径 (默认: ./models)\n";
    std::wcout << L"  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::wcout << L"  --gpu-workers <num>   GPU Worker数量 (默认: 0)\n";
    std::wcout << L"  --cpu-workers <num>   CPU Worker数量 (默认: 1)\n";
    std::wcout << L"  --help                显示此帮助信息\n";
    std::wcout << L"\n示例:\n";
    std::wcout << L"  ocr_service --model-dir ./models --pipe-name \\\\.\\pipe\\ocr_service\n";
    std::wcout << L"  ocr_service --cpu-workers 4\n";
    std::wcout << L"  ocr_service --gpu-workers 2\n";
    std::wcout << L"\n注意:\n";
    std::wcout << L"  可以使用 'ocr_client --shutdown' 命令优雅关闭服务\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    setupConsole();
#endif

    std::string model_dir = "./models";
    std::string pipe_name = "\\\\.\\pipe\\ocr_service";
    int gpu_workers = 0;  // 默认0个GPU Worker, 使用CPU处理
    int cpu_workers = 1;  // 默认1个CPU Worker
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        else if (arg == "--model-dir" && i + 1 < argc) {
            model_dir = argv[++i];
        }        else if (arg == "--pipe-name" && i + 1 < argc) {
            pipe_name = argv[++i];
        }
        else if (arg == "--gpu-workers" && i + 1 < argc) {
            gpu_workers = std::stoi(argv[++i]);
        }
        else if (arg == "--cpu-workers" && i + 1 < argc) {
            cpu_workers = std::stoi(argv[++i]);
        }        else {
            std::wcerr << L"Unknown argument: " << std::wstring(arg.begin(), arg.end()) << std::endl;
            printUsage();
            return 1;
        }
    }      std::wcout << L"=== PaddleOCR IPC Service ===" << std::endl;
    std::wcout << L"Model Directory: " << std::wstring(model_dir.begin(), model_dir.end()) << std::endl;
    std::wcout << L"Pipe Name: " << std::wstring(pipe_name.begin(), pipe_name.end()) << std::endl;
    std::wcout << L"GPU Workers: " << gpu_workers << std::endl;
    std::wcout << L"CPU Workers: " << cpu_workers << std::endl;
    std::wcout << L"==============================" << std::endl;
      try {
        // 设置控制台处理程序
        if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
            std::wcerr << L"Warning: Could not set console handler" << std::endl;
        }
        
        // 创建并启动服务
        g_service = std::make_unique<PaddleOCR::OCRIPCService>(model_dir, pipe_name, gpu_workers, cpu_workers);
        
        if (!g_service->start()) {
            std::wcerr << L"Failed to start OCR service" << std::endl;
            return 1;
        }
        std::wcout << L"OCR Service is running..." << std::endl;
        std::wcout << L"Press Ctrl+C to stop the service, or use 'ocr_client --shutdown'" << std::endl;
        
        // 主循环 - 定期输出状态信息
        while (g_service->isRunning()) {
            // 等待5秒并检查服务状态
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // 每30秒输出一次状态
            static int status_counter = 0;
            if (++status_counter >= 6) { // 5秒 * 6 = 30秒
                status_counter = 0;
                if (g_service->isRunning()) {
                    std::string status_info = g_service->getStatusInfo();
                    std::wstring status_wide = utf8ToWideString(status_info);
                    std::wcout << L"Service Status: " << status_wide << std::endl;
                }
            }
        }
        
        std::wcout << L"Service stopped gracefully" << std::endl;
    }
    catch (const std::exception& e) {
        std::wstring error_msg = utf8ToWideString(e.what());
        std::wcerr << L"Service error: " << error_msg << std::endl;
        return 1;
    }
    
    return 0;
}
