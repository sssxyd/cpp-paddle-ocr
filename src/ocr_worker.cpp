#include "paddle_ocr/ocr_worker.h"
#include <json/json.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

namespace PaddleOCR {

// OCRWorker 实现
OCRWorker::OCRWorker(int worker_id, const std::string& model_dir, bool use_gpu, int gpu_id, bool enable_cls)
    : worker_id_(worker_id), use_gpu_(use_gpu), gpu_id_(gpu_id), enable_cls_(enable_cls), running_(false), is_idle_(true) {
    
    try {
        // CPU线程数优化：减少每个worker的线程占用，提高多worker并发效率
        int det_threads = use_gpu ? 1 : 2;   // 检测器线程数：GPU=1, CPU=2（降低4->2）
        int cls_threads = use_gpu ? 1 : 1;   // 分类器线程数：GPU=1, CPU=1（降低2->1）  
        int rec_threads = use_gpu ? 1 : 2;   // 识别器线程数：GPU=1, CPU=2（降低4->2）
        
        // 初始化检测器 - 针对微信小程序截图优化
        detector_ = std::make_unique<DBDetector>(
            model_dir + "/det",
            use_gpu, gpu_id, 
            use_gpu ? 600 : 0,      // gpu_mem: 微信小程序512px，显存需求更少 (单位:MB)
            det_threads,            // cpu_math_library_num_threads: 优化多worker并发
            !use_gpu,               // use_mkldnn
            "max", 
            512,                    // max_side_len: 适配微信小程序500px宽度 (640->512)
            0.2,                    // det_db_thresh: 降低阈值，小程序UI对比度高 (0.3->0.2)
            0.4,                    // det_db_box_thresh: 降低框过滤阈值 (0.5->0.4)  
            1.8,                    // det_db_unclip_ratio: 减少扩展比例，小程序文字边界清晰 (2.0->1.8)
            "fast",                 // det_db_score_mode: 快速模式适合规整文字
            false,                  // use_polygon: 小程序截图不需要多边形检测
            use_gpu, "fp32"
        );
        
        // 初始化分类器（仅在启用时）- 微信小程序通常不需要方向分类
        if (enable_cls_) {
            classifier_ = std::make_unique<Classifier>(
                model_dir + "/cls",
                use_gpu, gpu_id, 
                use_gpu ? 250 : 0,   // gpu_mem: 微信小程序方向固定，分类器需求最少 (单位:MB)
                cls_threads,         // cpu_math_library_num_threads: 优化多worker并发
                !use_gpu,            // use_mkldnn
                0.98,                // cls_thresh: 进一步提高阈值，小程序方向极其确定 (0.95->0.98)
                use_gpu, "fp32", 
                8                    // cls_batch_num: 增加批处理，小程序处理更快 (6->8)
            );
        }
        
        // 初始化识别器 - 针对微信小程序规整文字优化
        recognizer_ = std::make_unique<CRNNRecognizer>(
            model_dir + "/rec",
            use_gpu, gpu_id, 
            use_gpu ? 400 : 0,      // gpu_mem: 微信小程序文字小且规整，识别器需求极少 (单位:MB)
            rec_threads,            // cpu_math_library_num_threads: 优化多worker并发
            !use_gpu,               // use_mkldnn
            model_dir + "/rec/ppocr_keys_v1.txt",
            use_gpu, "fp32",
            16,                     // rec_batch_num: 大幅增加批处理，小程序适合高并发 (12->16)
            28,                     // rec_img_h: 进一步降低高度，小程序文字通常较小 (32->28)
            192                     // rec_img_w: 进一步降低宽度，小程序文字简单 (224->192)
        );
        
        int total_memory = 0;
        if (use_gpu) {
            total_memory = 600 + 400; // 检测器 + 识别器 (微信小程序尺寸更小)
            if (enable_cls_) {
                total_memory += 250; // + 分类器
            }
        } else {
            // CPU模式：模型文件 + 运行时缓存的估算内存占用
            total_memory = 60 + 40; // 检测模型 + 识别模型 (单位:MB)
            if (enable_cls_) {
                total_memory += 20; // + 分类模型
            }
            total_memory += 50; // + 运行时缓存和临时内存
        }
        
        std::cout << "OCRWorker " << worker_id_ << " initialized successfully (" 
                  << (use_gpu ? "GPU" : "CPU") << ", CLS: " << (enable_cls_ ? "ON" : "OFF");
        if (use_gpu) {
            std::cout << ", GPU Memory: " << total_memory << "MB";
        } else {
            // 正确计算峰值线程数：det/cls/rec顺序执行，取最大值
            int peak_threads = std::max({det_threads, cls_threads, rec_threads}) + 1; // +1 for worker main thread
            std::cout << ", Est. RAM Usage: ~" << total_memory << "MB"
                      << ", Peak Threads: " << peak_threads 
                      << " (det:" << det_threads << "→cls:" << cls_threads << "→rec:" << rec_threads << "+main:1)";
        }
        std::cout << ", Optimized for: WeChat Mini-Program Screenshots)" << std::endl;
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
                json_result["width"] = result.width;
                json_result["height"] = result.height;
                json_result["success"] = result.success;
                json_result["processing_time_ms"] = result.processing_time_ms;
                json_result["worker_id"] = worker_id_;
                
                if (result.success) {
                    Json::Value words_array(Json::arrayValue);
                    for (const auto& word : result.words) {
                        Json::Value word_map(Json::objectValue);
                        word_map["text"] = word.text;
                        word_map["confidence"] = word.confidence;
                        Json::Value box_array(Json::arrayValue);
                        for (const auto& point : word.box) {
                            Json::Value point_array(Json::arrayValue);
                            point_array.append(point[0]);
                            point_array.append(point[1]);
                            box_array.append(point_array);
                        }
                        word_map["box"] = box_array;
                        words_array.append(word_map);
                    }
                    json_result["words"] = words_array;                    
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
        result.width = image.cols;
        result.height = image.rows;
        
        // 文本检测
        std::vector<std::vector<std::vector<int>>> det_boxes;
        std::vector<double> det_times;
        detector_->Run(image, det_boxes, det_times);
        
        if (det_boxes.empty()) {
            result.success = true;
            result.words = {};
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
            result.words = {};
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
        
        // 文本方向分类（可选）
        if (enable_cls_ && classifier_) {
            std::vector<int> cls_labels(text_images.size());
            std::vector<float> cls_scores(text_images.size());
            std::vector<double> cls_times;
            classifier_->Run(text_images, cls_labels, cls_scores, cls_times);
            
            // 根据分类结果旋转图像
            for (size_t i = 0; i < text_images.size() && i < cls_labels.size(); ++i) {
                if (cls_labels[i] == 1) {  // 需要旋转180度
                    cv::rotate(text_images[i], text_images[i], cv::ROTATE_180);
                }
            }
        }
        // 如果不启用分类器，跳过文本方向检测，直接进行识别
        
        // 文本识别
        std::vector<std::string> rec_texts(text_images.size());
        std::vector<float> rec_scores(text_images.size());
        std::vector<double> rec_times;
        recognizer_->Run(text_images, rec_texts, rec_scores, rec_times);
        
        // 设置结果
        result.success = true;
        for (size_t i = 0; i < rec_texts.size(); i++)
        {
            WordResult word;
            word.text = rec_texts[i];
            word.confidence = rec_scores[i];
            word.box = det_boxes[i];
            result.words.push_back(word);
        }
        
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
    catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

std::string OCRWorker::getWorkerRecommendation(bool use_gpu, bool enable_cls) {
    unsigned int logical_cores = std::thread::hardware_concurrency();
    
    std::ostringstream oss;
    oss << "=== OCR Worker Configuration Recommendation ===\n";
    oss << "System Info:\n";
    oss << "  - Logical CPU Cores (Hardware Threads): " << logical_cores << "\n";
    
    if (use_gpu) {
        oss << "  - Mode: GPU (显存限制)\n";
        oss << "GPU Mode Recommendations:\n";
        if (enable_cls) {
            oss << "  - Memory per Worker: 1250MB GPU (with classifier)\n";
            oss << "  - 4GB GPU: Max 2-3 Workers\n";
            oss << "  - 8GB GPU: Max 5-6 Workers\n";
            oss << "  - 12GB GPU: Max 8-9 Workers\n";
        } else {
            oss << "  - Memory per Worker: 1000MB GPU (no classifier)\n";
            oss << "  - 4GB GPU: Max 3-4 Workers\n";
            oss << "  - 8GB GPU: Max 6-7 Workers\n";
            oss << "  - 12GB GPU: Max 10-11 Workers\n";
        }
    } else {
        oss << "  - Mode: CPU (线程数限制)\n";
        
        // 正确的线程占用分析：det/cls/rec是顺序执行，不是并行
        int det_threads = 2;  // 检测器线程数
        int cls_threads = enable_cls ? 1 : 0;  // 分类器线程数（如果启用）
        int rec_threads = 2;  // 识别器线程数
        int main_thread = 1;  // 主协调线程
        
        // 实际峰值线程数：det/cls/rec顺序执行，取最大值 + 主线程
        int peak_threads_per_worker = std::max({det_threads, cls_threads, rec_threads}) + main_thread;
        
        // 基于峰值线程数计算Worker数量
        int conservative_workers = std::max(1, static_cast<int>(logical_cores * 0.6 / peak_threads_per_worker));
        int recommended_workers = std::max(1, static_cast<int>(logical_cores * 0.8 / peak_threads_per_worker));
        int aggressive_workers = std::max(2, static_cast<int>(logical_cores * 1.0 / peak_threads_per_worker));
        
        // 对于常见CPU进行特殊优化
        if (logical_cores == 8) {
            // 4核8线程：8÷3=2.67，可以安全开2-3个Worker
            conservative_workers = 2;
            recommended_workers = enable_cls ? 2 : 3;  // 无分类器时可以更激进
            aggressive_workers = 3;
        } else if (logical_cores == 4) {
            // 4核4线程：4÷3=1.33，可以开1-2个Worker
            conservative_workers = 1;
            recommended_workers = 1;
            aggressive_workers = 2;
        } else if (logical_cores >= 12) {
            // 12线程以上的CPU可以更激进
            conservative_workers = std::max(3, conservative_workers);
            recommended_workers = std::max(4, recommended_workers);
            aggressive_workers = std::max(5, aggressive_workers);
        } else if (logical_cores >= 16) {
            // 16线程以上的CPU
            conservative_workers = std::max(4, conservative_workers);
            recommended_workers = std::max(6, recommended_workers);
            aggressive_workers = std::max(8, aggressive_workers);
        }
        
        oss << "CPU Mode Recommendations:\n";
        oss << "  - 线程占用分析: det(" << det_threads << ") → cls(" << cls_threads 
            << ") → rec(" << rec_threads << ") + main(" << main_thread << ")\n";
        oss << "  - 峰值并发线程: " << peak_threads_per_worker << " threads/worker (顺序执行，非并行)\n";
        oss << "  - Memory per Worker: ~" << (enable_cls ? 170 : 150) << "MB RAM\n";
        oss << "  - Conservative: " << conservative_workers << " Workers (稳定运行)\n";
        oss << "  - Recommended: " << recommended_workers << " Workers (推荐配置)\n";
        oss << "  - Aggressive: " << aggressive_workers << " Workers (最大吞吐)\n";
        
        // 添加线程计算说明
        oss << "\n  线程计算说明:\n";
        oss << "  - OCR流程: 检测→分类→识别 (顺序执行)\n";
        oss << "  - 实际并发: max(det,cls,rec) + main = " << peak_threads_per_worker << " threads\n";
        oss << "  - 总线程占用: " << recommended_workers << " workers × " 
            << peak_threads_per_worker << " = " << (recommended_workers * peak_threads_per_worker) << " threads\n";
    }
    
    oss << "\nNote: 以上基于逻辑核心数(" << logical_cores << ")计算，包含超线程/SMT";
    
    return oss.str();
}

} // namespace PaddleOCR
