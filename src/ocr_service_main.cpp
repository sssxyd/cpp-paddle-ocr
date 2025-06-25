#include "paddle_ocr/ocr_service.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <windows.h>

std::unique_ptr<PaddleOCR::OCRIPCService> g_service;
HANDLE g_shutdown_event = NULL;

// 信号处理函数
BOOL WINAPI ConsoleHandler(DWORD dwType) {
    switch (dwType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::cout << "\nReceived shutdown signal, stopping service..." << std::endl;
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
    std::cout << "OCR IPC Service\n";
    std::cout << "Usage: ocr_service [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --model-dir <path>    模型文件目录路径 (默认: ./models)\n";
    std::cout << "  --pipe-name <name>    命名管道名称 (默认: \\\\.\\pipe\\ocr_service)\n";
    std::cout << "  --gpu-workers <num>   GPU Worker数量 (默认: 0)\n";
    std::cout << "  --cpu-workers <num>   CPU Worker数量 (默认: 1)\n";
    std::cout << "  --shutdown-event <name>  关闭事件名称 (用于优雅关闭)\n";
    std::cout << "  --help                显示此帮助信息\n";
    std::cout << "\n示例:\n";
    std::cout << "  ocr_service --model-dir ./models --pipe-name \\\\.\\pipe\\my_ocr\n";
    std::cout << "  ocr_service --cpu-workers 4\n";
    std::cout << "  ocr_service --gpu-workers 2  # 使用2个GPU Worker\n";
    std::cout << "  ocr_service --shutdown-event Global\\\\OCRServiceShutdown\n";
}

int main(int argc, char* argv[]) {
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
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return 1;
        }
    }    
    std::cout << "=== PaddleOCR IPC Service ===" << std::endl;
    std::cout << "Model Directory: " << model_dir << std::endl;
    std::cout << "Pipe Name: " << pipe_name << std::endl;
    std::cout << "GPU Workers: " << gpu_workers << std::endl;
    std::cout << "CPU Workers: " << cpu_workers << std::endl;
    if (!shutdown_event_name.empty()) {
        std::cout << "Shutdown Event: " << shutdown_event_name << std::endl;
    }
    std::cout << "==============================" << std::endl;
      try {
        // 创建或打开关闭事件
        if (!shutdown_event_name.empty()) {
            g_shutdown_event = CreateEventA(NULL, TRUE, FALSE, shutdown_event_name.c_str());
            if (g_shutdown_event == NULL) {
                std::cerr << "Warning: Could not create shutdown event: " << shutdown_event_name << std::endl;
            } else {
                std::cout << "Monitoring shutdown event: " << shutdown_event_name << std::endl;
            }
        }
        
        // 设置控制台处理程序
        if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
            std::cerr << "Warning: Could not set console handler" << std::endl;
        }// 创建并启动服务
        g_service = std::make_unique<PaddleOCR::OCRIPCService>(model_dir, pipe_name, gpu_workers, cpu_workers);
        
        if (!g_service->start()) {
            std::cerr << "Failed to start OCR service" << std::endl;
            return 1;
        }
        std::cout << "OCR Service is running..." << std::endl;
        std::cout << "Press Ctrl+C to stop the service" << std::endl;
        
        // 主循环 - 监听关闭事件和定期输出状态信息
        while (g_service->isRunning()) {
            DWORD wait_result = WAIT_TIMEOUT;
              if (g_shutdown_event) {
                // 等待关闭事件或超时 (延长到10秒，减少唤醒频率)
                wait_result = WaitForSingleObject(g_shutdown_event, 10000); // 10秒超时
            } else {
                // 没有关闭事件，只是简单等待
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
            
            if (wait_result == WAIT_OBJECT_0) {
                // 收到关闭信号
                std::cout << "Received shutdown event, stopping service..." << std::endl;
                g_service->stop();
                break;            } else if (wait_result == WAIT_TIMEOUT) {
                // 超时，继续运行并输出状态（每30秒一次）
                static int status_counter = 0;
                if (++status_counter >= 3) { // 10秒 * 3 = 30秒
                    status_counter = 0;
                    if (g_service->isRunning()) {
                        std::cout << "Service Status: " << g_service->getStatusInfo() << std::endl;
                    }
                }
            }
        }        
        std::cout << "Service stopped gracefully" << std::endl;
        
        // 清理资源
        if (g_shutdown_event) {
            CloseHandle(g_shutdown_event);
            g_shutdown_event = NULL;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Service error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
