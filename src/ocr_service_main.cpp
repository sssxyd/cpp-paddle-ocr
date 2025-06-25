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
HANDLE g_shutdown_event = NULL;

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
        if (g_shutdown_event) {
            SetEvent(g_shutdown_event);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

void printUsage() {
    std::wcout << L"OCR IPC Service\n";
    std::wcout << L"Usage: ocr_service [options]\n";
    std::wcout << L"\nOptions:\n";
    std::wcout << L"  --model-dir <path>    模型文件目录路径 (默认: ./models)\n";
    std::wcout << L"  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::wcout << L"  --gpu-workers <num>   GPU Worker数量 (默认: 0)\n";
    std::wcout << L"  --cpu-workers <num>   CPU Worker数量 (默认: 1)\n";
    std::wcout << L"  --shutdown-event <name>  关闭事件名称 (用于优雅关闭)\n";
    std::wcout << L"  --help                显示此帮助信息\n";
    std::wcout << L"\n示例:\n";
    std::wcout << L"  ocr_service --model-dir ./models --pipe-name \\\\.\\pipe\\my_ocr\n";
    std::wcout << L"  ocr_service --cpu-workers 4\n";
    std::wcout << L"  ocr_service --gpu-workers 2\n";
    std::wcout << L"  ocr_service --shutdown-event Global\\\\OCRServiceShutdown\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    setupConsole();
#endif

    std::string model_dir = "./models";
    std::string pipe_name = "\\\\.\\pipe\\ocr_service";
    std::string shutdown_event_name = "";
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
        else if (arg == "--shutdown-event" && i + 1 < argc) {
            shutdown_event_name = argv[++i];
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
    if (!shutdown_event_name.empty()) {
        std::wcout << L"Shutdown Event: " << std::wstring(shutdown_event_name.begin(), shutdown_event_name.end()) << std::endl;
    }
    std::wcout << L"==============================" << std::endl;
      try {        // 创建或打开关闭事件
        if (!shutdown_event_name.empty()) {
            g_shutdown_event = CreateEventA(NULL, TRUE, FALSE, shutdown_event_name.c_str());
            if (g_shutdown_event == NULL) {
                std::wcerr << L"Warning: Could not create shutdown event: " << std::wstring(shutdown_event_name.begin(), shutdown_event_name.end()) << std::endl;
            } else {
                std::wcout << L"Monitoring shutdown event: " << std::wstring(shutdown_event_name.begin(), shutdown_event_name.end()) << std::endl;
            }
        }
        
        // 设置控制台处理程序
        if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
            std::wcerr << L"Warning: Could not set console handler" << std::endl;
        }        // 创建并启动服务
        g_service = std::make_unique<PaddleOCR::OCRIPCService>(model_dir, pipe_name, gpu_workers, cpu_workers);
        
        if (!g_service->start()) {
            std::wcerr << L"Failed to start OCR service" << std::endl;
            return 1;
        }
        std::wcout << L"OCR Service is running..." << std::endl;
        std::wcout << L"Press Ctrl+C to stop the service" << std::endl;
        
        // 主循环 - 监听关闭事件和定期输出状态信息
        while (g_service->isRunning()) {
            DWORD wait_result = WAIT_TIMEOUT;
            
            if (g_shutdown_event) {
                // 等待关闭事件或超时
                wait_result = WaitForSingleObject(g_shutdown_event, 5000); // 5秒超时
            } else {
                // 没有关闭事件，只是简单等待
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
              if (wait_result == WAIT_OBJECT_0) {
                // 收到关闭信号
                std::wcout << L"Received shutdown event, stopping service..." << std::endl;
                g_service->stop();
                break;
            } else if (wait_result == WAIT_TIMEOUT) {
                // 超时，继续运行并输出状态（每30秒一次）
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
        }        
        std::wcout << L"Service stopped gracefully" << std::endl;
        
        // 清理资源
        if (g_shutdown_event) {
            CloseHandle(g_shutdown_event);
            g_shutdown_event = NULL;
        }
    }
    catch (const std::exception& e) {
        std::wstring error_msg = utf8ToWideString(e.what());
        std::wcerr << L"Service error: " << error_msg << std::endl;
        return 1;
    }
    
    return 0;
}
