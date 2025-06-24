#include "paddle_ocr/ocr_worker.h"
#include <json/json.h>
#include <iostream>
#include <chrono>

namespace PaddleOCR {

// OCRWorker 实现
OCRWorker::OCRWorker(int worker_id, const std::string& model_dir, bool use_gpu, int gpu_id)
    : worker_id_(worker_id), use_gpu_(use_gpu), gpu_id_(gpu_id), running_(false), is_idle_(true) {
    
    try {
        // 初始化检测器
        detector_ = std::make_unique<DBDetector>(
            model_dir + "/det",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            "max", 960, 0.3, 0.5, 2.0, "slow", false,
            use_gpu, "fp32"
        );
        
        // 初始化分类器
        classifier_ = std::make_unique<Classifier>(
            model_dir + "/cls",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            0.9, use_gpu, "fp32", 1
        );
        
        // 初始化识别器
        recognizer_ = std::make_unique<CRNNRecognizer>(
            model_dir + "/rec",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            model_dir + "/rec/ppocr_keys_v1.txt",
            use_gpu, "fp32", 6, 48, 320
        );
        
        std::cout << "OCRWorker " << worker_id_ << " initialized successfully (" 
                  << (use_gpu ? "GPU" : "CPU") << ")" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to initialize OCRWorker " << worker_id_ 
                  << ": " << e.what() << std::endl;
        throw;
    }
}

OCRWorker::~OCRWorker() {
    stop();
}

void OCRWorker::start() {
    if (running_) return;
    
    running_ = true;
    worker_thread_ = std::thread(&OCRWorker::workerLoop, this);
    std::cout << "OCRWorker " << worker_id_ << " started" << std::endl;
}

void OCRWorker::stop() {
    if (!running_) return;
    
    running_ = false;
    cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::cout << "OCRWorker " << worker_id_ << " stopped" << std::endl;
}

void OCRWorker::addRequest(std::shared_ptr<OCRRequest> request) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push(request);
    }
    cv_.notify_one();
}

void OCRWorker::workerLoop() {
    while (running_) {
        std::shared_ptr<OCRRequest> request;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return !request_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            if (!request_queue_.empty()) {
                request = request_queue_.front();
                request_queue_.pop();
                is_idle_ = false;
            }
        }
        
        if (request) {
            try {
                auto result = processRequest(*request);
                
                // 构建结果字符串
                Json::Value json_result;
                json_result["request_id"] = result.request_id;
                json_result["success"] = result.success;
                json_result["processing_time_ms"] = result.processing_time_ms;
                json_result["worker_id"] = worker_id_;
                
                if (result.success) {
                    Json::Value texts_array(Json::arrayValue);
                    for (const auto& text : result.texts) {
                        texts_array.append(text);
                    }
                    json_result["texts"] = texts_array;
                    
                    Json::Value boxes_array(Json::arrayValue);
                    for (const auto& box : result.boxes) {
                        Json::Value box_array(Json::arrayValue);
                        for (const auto& point : box) {
                            Json::Value point_array(Json::arrayValue);
                            point_array.append(point[0]);
                            point_array.append(point[1]);
                            box_array.append(point_array);
                        }
                        boxes_array.append(box_array);
                    }
                    json_result["boxes"] = boxes_array;
                } else {
                    json_result["error"] = result.error_message;
                }
                
                Json::StreamWriterBuilder builder;
                request->result_promise.set_value(Json::writeString(builder, json_result));
            }
            catch (const std::exception& e) {
                Json::Value error_result;
                error_result["request_id"] = request->request_id;
                error_result["success"] = false;
                error_result["error"] = e.what();
                error_result["worker_id"] = worker_id_;
                
                Json::StreamWriterBuilder builder;
                request->result_promise.set_value(Json::writeString(builder, error_result));
            }
            
            is_idle_ = true;
        }
    }
}

OCRResult OCRWorker::processRequest(const OCRRequest& request) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    OCRResult result;
    result.request_id = request.request_id;
    result.success = false;
      try {
        cv::Mat image = request.image_data;
        
        // 验证图像数据
        if (image.empty()) {
            result.error_message = "Empty image data provided";
            return result;
        }
        
        // 文本检测
        std::vector<std::vector<std::vector<int>>> det_boxes;
        std::vector<double> det_times;
        detector_->Run(image, det_boxes, det_times);
        
        if (det_boxes.empty()) {
            result.success = true;
            result.texts = {};
            result.boxes = {};
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
        
        // 提取文本区域图像
        std::vector<cv::Mat> text_images;
        for (const auto& box : det_boxes) {
            // 创建ROI
            std::vector<cv::Point2f> points;
            for (const auto& point : box) {
                points.emplace_back(point[0], point[1]);
            }
            
            // 计算边界矩形
            cv::Rect bbox = cv::boundingRect(points);
            bbox &= cv::Rect(0, 0, image.cols, image.rows);  // 确保在图像范围内
            
            if (bbox.width > 0 && bbox.height > 0) {
                text_images.push_back(image(bbox));
            }
        }
        
        if (text_images.empty()) {
            result.success = true;
            result.texts = {};
            result.boxes = {};
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
        
        // 文本方向分类
        std::vector<int> cls_labels;
        std::vector<float> cls_scores;
        std::vector<double> cls_times;
        classifier_->Run(text_images, cls_labels, cls_scores, cls_times);
        
        // 根据分类结果旋转图像
        for (size_t i = 0; i < text_images.size() && i < cls_labels.size(); ++i) {
            if (cls_labels[i] == 1) {  // 需要旋转180度
                cv::rotate(text_images[i], text_images[i], cv::ROTATE_180);
            }
        }
        
        // 文本识别
        std::vector<std::string> rec_texts;
        std::vector<float> rec_scores;
        std::vector<double> rec_times;
        recognizer_->Run(text_images, rec_texts, rec_scores, rec_times);
        
        // 设置结果
        result.success = true;
        result.texts = rec_texts;
        result.boxes = det_boxes;
        result.confidences = rec_scores;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
    catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

} // namespace PaddleOCR
