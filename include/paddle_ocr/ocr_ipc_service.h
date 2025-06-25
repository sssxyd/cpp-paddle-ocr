#pragma once

#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include "ocr_worker.h"
#include "gpu_worker_pool.h"
#include "cpu_worker_pool.h"

namespace PaddleOCR {

/**
 * @brief IPC OCR 服务
 */
class OCRIPCService {
public:
    /**
     * @brief 构造 IPC OCR 服务
     * 
     * @param model_dir 模型文件目录路径
     * @param pipe_name 命名管道名称 (Windows)
     * @param gpu_workers GPU Worker 数量 (默认: 0)
     * @param cpu_workers CPU Worker 数量 (默认: 1)
     */
    explicit OCRIPCService(const std::string& model_dir, 
                          const std::string& pipe_name = "\\\\.\\pipe\\ocr_service",
                          int gpu_workers = 0,
                          int cpu_workers = 1);
    
    ~OCRIPCService();
    
    /**
     * @brief 启动 IPC 服务
     */
    bool start();
    
    /**
     * @brief 停止 IPC 服务
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_; }
    
    /**
     * @brief 获取服务状态信息
     */
    std::string getStatusInfo() const;

private:
    // IPC 相关
    void ipcServerLoop();
    void handleClientConnection(HANDLE pipe_handle);
    void cleanupFinishedClientThreads();
    std::string processIPCRequest(const std::string& request_json);
    
    // Base64 编码/解码辅助函数
    static std::vector<uchar> base64Decode(const std::string& encoded);
    static cv::Mat base64ToMat(const std::string& base64_string);
    
      // 请求处理（统一转换为cv::Mat后传递给worker）
    std::future<std::string> processOCRRequest(const cv::Mat& image);
    
    std::string model_dir_;
    std::string pipe_name_;
    int gpu_workers_;
    int cpu_workers_;
    std::atomic<bool> running_;
    std::atomic<int> request_counter_;

    // Worker 管理
    std::unique_ptr<GPUWorkerPool> gpu_worker_pool_;
    std::unique_ptr<CPUWorkerPool> cpu_worker_pool_;
      // IPC 线程
    std::thread ipc_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex client_threads_mutex_;
    
    // 管道缓冲区配置常量
    static const int PIPE_OUTPUT_BUFFER_SIZE = 65536;    // 64KB - OCR结果通常很小
    static const int PIPE_INPUT_BUFFER_SIZE = 1048576;   // 1MB - 需要接收大图像数据
    static const int READ_BUFFER_SIZE = 1048576;         // 1MB - 应用层读取缓冲区，与管道输入缓冲区匹配
    
    // 统计信息
    std::atomic<int> total_requests_;
    std::atomic<int> successful_requests_;
    std::atomic<double> total_processing_time_;
};

} // namespace PaddleOCR
