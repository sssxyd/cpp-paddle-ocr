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
    cv::Mat image_data;                 // 统一使用cv::Mat存储图像数据
    std::promise<std::string> result_promise;
    
    // 构造函数：使用cv::Mat（worker只需要处理这一种情况）
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
    OCRWorker(int worker_id, const std::string& model_dir, bool use_gpu, int gpu_id = 0, bool enable_cls = false);
    virtual ~OCRWorker();
    
    void start();
    void stop();
    void addRequest(std::shared_ptr<OCRRequest> request);
    bool isIdle() const { return is_idle_; }
    int getWorkerId() const { return worker_id_; }
    
    /**
     * @brief 获取系统CPU信息和建议的Worker数量
     * @param use_gpu 是否使用GPU模式
     * @param enable_cls 是否启用分类器
     * @return 包含系统信息和建议的字符串
     */
    static std::string getWorkerRecommendation(bool use_gpu, bool enable_cls = false);
    
private:
    void workerLoop();
    OCRResult processRequest(const OCRRequest& request);
    
    int worker_id_;
    bool use_gpu_;
    int gpu_id_;
    bool enable_cls_;  // 是否启用文本方向分类
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
