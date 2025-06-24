#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <atomic>
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
 * @brief OCR Worker 类
 * 负责执行OCR任务的工作线程
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

} // namespace PaddleOCR
