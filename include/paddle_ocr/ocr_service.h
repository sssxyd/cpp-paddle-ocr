#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <atomic>
#include <windows.h>
#include <opencv2/opencv.hpp>

#include "ocr_det.h"
#include "ocr_rec.h"
#include "ocr_cls.h"

namespace PaddleOCR {

/**
 * @brief OCR 任务请求结构
 */
struct OCRRequest {
    int request_id;
    std::string image_path;
    cv::Mat image_data;
    std::promise<std::string> result_promise;
    
    OCRRequest(int id, const std::string& path) 
        : request_id(id), image_path(path) {}
    
    OCRRequest(int id, const cv::Mat& img) 
        : request_id(id), image_data(img.clone()) {}
};

/**
 * @brief OCR 处理结果
 */
struct OCRResult {
    int request_id;
    bool success;
    std::string error_message;
    std::vector<std::string> texts;
    std::vector<std::vector<std::vector<int>>> boxes;
    std::vector<float> confidences;
    double processing_time_ms;
};

/**
 * @brief OCR Worker 基类
 */
class OCRWorker {
public:
    OCRWorker(int worker_id, const std::string& model_dir, bool use_gpu, int gpu_id = 0);
    virtual ~OCRWorker();
    
    void start();
    void stop();
    void addRequest(std::shared_ptr<OCRRequest> request);
    bool isIdle() const { return is_idle_; }
    int getWorkerId() const { return worker_id_; }
    
private:
    void workerLoop();
    OCRResult processRequest(const OCRRequest& request);
    
    int worker_id_;
    bool use_gpu_;
    int gpu_id_;
    std::atomic<bool> running_;
    std::atomic<bool> is_idle_;
    
    std::thread worker_thread_;
    std::queue<std::shared_ptr<OCRRequest>> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    
    // OCR 组件
    std::unique_ptr<DBDetector> detector_;
    std::unique_ptr<Classifier> classifier_;
    std::unique_ptr<CRNNRecognizer> recognizer_;
};

/**
 * @brief GPU Worker Pool (支持单GPU多线程)
 */
class GPUWorkerPool {
public:
    GPUWorkerPool(const std::string& model_dir, int num_workers = 2);
    ~GPUWorkerPool();
    
    void start();
    void stop();
    std::future<std::string> submitRequest(std::shared_ptr<OCRRequest> request);
    
    int getOptimalWorkerCount();  // 根据GPU内存自动计算最优Worker数量
    
private:
    OCRWorker* getAvailableWorker();
    bool checkGPUMemory(int num_workers);  // 检查GPU内存是否足够
    
    std::vector<std::unique_ptr<OCRWorker>> workers_;
    std::mutex workers_mutex_;
    std::atomic<int> next_worker_index_;
    int gpu_memory_per_worker_mb_;
};

/**
 * @brief CPU Worker Pool
 */
class CPUWorkerPool {
public:
    CPUWorkerPool(const std::string& model_dir, int num_workers);
    ~CPUWorkerPool();
    
    void start();
    void stop();
    std::future<std::string> submitRequest(std::shared_ptr<OCRRequest> request);
    
private:
    OCRWorker* getAvailableWorker();
    
    std::vector<std::unique_ptr<OCRWorker>> workers_;
    std::mutex workers_mutex_;
    std::atomic<int> next_worker_index_;
};

/**
 * @brief IPC OCR 服务
 */
class OCRIPCService {
public:    /**
     * @brief 构造 IPC OCR 服务
     * 
     * @param model_dir 模型文件目录路径
     * @param pipe_name 命名管道名称 (Windows)
     * @param force_cpu 强制使用 CPU 模式
     * @param gpu_workers GPU Worker 数量 (默认: 1)
     * @param cpu_workers CPU Worker 数量 (默认: 1)
     */
    explicit OCRIPCService(const std::string& model_dir, 
                          const std::string& pipe_name = "\\\\.\\pipe\\ocr_service",
                          bool force_cpu = false,
                          int gpu_workers = 1,
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
    std::string processIPCRequest(const std::string& request_json);
      // 硬件检测
    bool detectGPU();
    int getCPUCoreCount();
    int getGPUMemory();
    
    // 请求处理
    std::future<std::string> processOCRRequest(const std::string& image_path);
    std::future<std::string> processOCRRequest(const cv::Mat& image);      std::string model_dir_;
    std::string pipe_name_;
    bool force_cpu_;
    int gpu_workers_;
    int cpu_workers_;
    std::atomic<bool> running_;
    std::atomic<int> request_counter_;
    
    // 硬件信息
    bool has_gpu_;
    int cpu_cores_;
    int gpu_memory_mb_;
    
    // Worker 管理
    std::unique_ptr<GPUWorkerPool> gpu_worker_pool_;
    std::unique_ptr<CPUWorkerPool> cpu_worker_pool_;
    
    // IPC 线程
    std::thread ipc_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex client_threads_mutex_;
    
    // 统计信息
    std::atomic<int> total_requests_;
    std::atomic<int> successful_requests_;
    std::atomic<double> total_processing_time_;
};

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
    std::string recognizeImage(const cv::Mat& image);
    
    bool isConnected() const { return connected_; }

private:
    std::string sendRequest(const std::string& request_json);
    
    std::string pipe_name_;
    HANDLE pipe_handle_;
    bool connected_;
    std::mutex comm_mutex_;
};

} // namespace PaddleOCR
