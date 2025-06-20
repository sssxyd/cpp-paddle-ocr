#include "paddle_ocr.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <algorithm>

using paddle_infer::Config;
using paddle_infer::Predictor;
using paddle_infer::CreatePredictor;
using paddle_infer::PrecisionType;

// 简单的日志宏
#define CHECK(condition) \
    if (!(condition)) { \
        std::cerr << "Check failed: " #condition << std::endl; \
        std::abort(); \
    }

#define LOG(level) std::cout << "[" #level "] "

// 构造函数
OcrEngine::OcrEngine() = default;

// 析构函数
OcrEngine::~OcrEngine() = default;

// 初始化
bool OcrEngine::Init(const std::string& det_model_dir,
                     const std::string& cls_model_dir, 
                     const std::string& rec_model_dir) {
    this->det_model_dir_ = det_model_dir;
    this->cls_model_dir_ = cls_model_dir;
    this->rec_model_dir_ = rec_model_dir;
    
    try {
        this->det_predictor_ = create_det_predictor();
        this->cls_predictor_ = create_cls_predictor();
        this->rec_predictor_ = create_rec_predictor();
        
        if (!det_predictor_ || !cls_predictor_ || !rec_predictor_) {
            std::cerr << "Failed to create predictors" << std::endl;
            return false;
        }
        
        LOG(INFO) << "OCR Engine initialized successfully";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Init failed: " << e.what() << std::endl;
        return false;
    }
}

// 处理图像
std::vector<OcrResult> OcrEngine::Process(const unsigned char* gray_img_bytes, 
                                          int width, int height) {
    std::vector<OcrResult> results;
    
    if (!gray_img_bytes || width <= 0 || height <= 0) {
        std::cerr << "Invalid input parameters" << std::endl;
        return results;
    }
    
    try {
        // 1. 转换为OpenCV Mat
        cv::Mat image = read_as_mat(gray_img_bytes, width, height);
        if (image.empty()) {
            std::cerr << "Failed to create Mat from image bytes" << std::endl;
            return results;
        }
        
        // 2. 文本检测
        auto text_boxes = detect_text(image, width, height);

        for (const auto& box : text_boxes) {
            if (box.size() < 4) continue;  // 确保有足够的点

            cv::Rect rect(box[0], box[1], box[2] - box[0], box[3] - box[1]);
            std::cout << "Detected text box: " 
                      << "x=" << rect.x << ", y=" << rect.y 
                      << ", width=" << rect.width 
                      << ", height=" << rect.height << std::endl;
        }
        
        // 3. 对每个检测到的文本框进行分类和识别
        // for (const auto& box : text_boxes) {
        //     if (box.size() < 4) continue;
            
        //     // 裁剪文本区域
        //     cv::Rect rect(box[0], box[1], box[2] - box[0], box[3] - box[1]);
        //     rect &= cv::Rect(0, 0, image.cols, image.rows); // 确保在图像范围内
            
        //     if (rect.width <= 0 || rect.height <= 0) continue;
            
        //     cv::Mat roi = image(rect).clone();
            
        //     // 方向分类和校正
        //     float cls_score = 0.0f;
        //     int cls_label = ClassifyAngle(roi, cls_score);
        //     if (cls_label == 180) {
        //         cv::flip(roi, roi, -1);  // 180度旋转
        //     }
            
        //     // 文本识别
        //     auto text_result = RecognizeText(roi);
            
        //     OcrResult result;
        //     result.text = text_result.first;
        //     result.score = text_result.second;
        //     result.rect = box;
            
        //     if (result.score > 0.5f && !result.text.empty()) {  // 置信度阈值
        //         results.push_back(result);
        //     }
        // }
        
    } catch (const std::exception& e) {
        std::cerr << "Process failed: " << e.what() << std::endl;
    }
    
    return results;
}

// 创建检测模型
std::shared_ptr<paddle_infer::Predictor> OcrEngine::create_det_predictor() {
    Config config;
    
    // 检查模型文件是否存在
    std::string model_file = det_model_dir_ + "/inference.pdmodel";
    std::string params_file = det_model_dir_ + "/inference.pdiparams";
    
    config.SetModel(model_file, params_file);
    config.EnableMKLDNN();
    config.SetCpuMathLibraryNumThreads(4);
    config.EnableMemoryOptim();
    config.SwitchIrOptim(true);
    
    return CreatePredictor(config);
} 

// 创建分类模型
std::shared_ptr<paddle_infer::Predictor> OcrEngine::create_cls_predictor() {
    Config config;
    
    std::string model_file = cls_model_dir_ + "/inference.pdmodel";
    std::string params_file = cls_model_dir_ + "/inference.pdiparams";
    
    config.SetModel(model_file, params_file);
    config.EnableMKLDNN();
    config.SetCpuMathLibraryNumThreads(4);
    config.EnableMemoryOptim();
    config.SwitchIrOptim(true);
    
    return CreatePredictor(config);
}

// 创建识别模型
std::shared_ptr<paddle_infer::Predictor> OcrEngine::create_rec_predictor() {
    Config config;
    
    std::string model_file = rec_model_dir_ + "/inference.pdmodel";
    std::string params_file = rec_model_dir_ + "/inference.pdiparams";
    
    config.SetModel(model_file, params_file);
    config.EnableMKLDNN();
    config.SetCpuMathLibraryNumThreads(4);
    config.EnableMemoryOptim();
    config.SwitchIrOptim(true);
    
    return CreatePredictor(config);
}

// 从字节数据读取为Mat（假设输入是灰度图）
cv::Mat OcrEngine::read_as_mat(const unsigned char* gray_img_bytes, int w, int h) {
    if (!gray_img_bytes || w <= 0 || h <= 0) {
        return cv::Mat();
    }
    
    // 创建灰度图Mat
    cv::Mat result = cv::Mat(h, w, CV_8UC1, const_cast<unsigned char*>(gray_img_bytes)).clone();
    
    // 需要RGB，转换为3通道
    if (result.channels() == 1) {
        cv::Mat rgb_image;
        cv::cvtColor(result, rgb_image, cv::COLOR_GRAY2BGR);
        result = rgb_image;
    }
    return result;
}

// 文本检测
std::vector<std::vector<int>> OcrEngine::detect_text(cv::Mat& image, int w, int h) {
    std::vector<std::vector<int>> text_boxes;
    
    try {
        // 预处理图像
        cv::Mat resized;
        int target_size = 640;
        double scale = std::min(static_cast<double>(target_size) / w, 
                               static_cast<double>(target_size) / h);
        int new_w = static_cast<int>(w * scale);
        int new_h = static_cast<int>(h * scale);
        
        cv::resize(image, resized, cv::Size(new_w, new_h));
        
        // 填充到目标尺寸
        cv::Mat padded;
        cv::copyMakeBorder(resized, padded, 0, target_size - new_h, 
                          0, target_size - new_w, cv::BORDER_CONSTANT, cv::Scalar(0));
        
        // 归一化
        padded.convertTo(padded, CV_32F, 1.0 / 255.0);

        // 准备输入数据
        std::vector<float> input_data(3 * target_size * target_size);
        float means[3] = {0.485f, 0.456f, 0.406f};
        float stds[3] = {0.229f, 0.224f, 0.225f};
        
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < target_size * target_size; ++i) {
                int row = i / target_size;
                int col = i % target_size;
                float pixel_value = padded.at<cv::Vec3f>(row, col)[c];
                input_data[c * target_size * target_size + i] = (pixel_value - means[c]) / stds[c];
            }
        }
        
        // 设置输入
        auto input_names = det_predictor_->GetInputNames();
        auto input_t = det_predictor_->GetInputHandle(input_names[0]);
        input_t->Reshape({1, 3, target_size, target_size});
        input_t->CopyFromCpu(input_data.data());
        
        // 执行推理
        CHECK(det_predictor_->Run());
        
        // 获取输出
        auto output_names = det_predictor_->GetOutputNames();
        auto output_t = det_predictor_->GetOutputHandle(output_names[0]);
        std::vector<int> output_shape = output_t->shape();
        
        int output_size = 1;
        for (int dim : output_shape) {
            output_size *= dim;
        }
        
        std::vector<float> output_data(output_size);
        output_t->CopyToCpu(output_data.data());
        
        // 后处理 - 简化版本，返回整个图像作为一个文本区域
        // 在实际应用中，这里需要实现DBNet等后处理算法
        if (!output_data.empty()) {
            text_boxes.push_back({0, 0, w, h});
        }
        
    } catch (const std::exception& e) {
        std::cerr << "DetectText failed: " << e.what() << std::endl;
        // 返回整个图像作为默认结果
        text_boxes.push_back({0, 0, w, h});
    }
    
    return text_boxes;
}

// 方向分类
int OcrEngine::classify_angle(cv::Mat& roi, float& score) {
    try {
        // 预处理
        cv::Mat resized;
        cv::resize(roi, resized, cv::Size(192, 48));
        resized.convertTo(resized, CV_32F, 1.0 / 255.0);
        
        // 归一化
        std::vector<float> input_data(3 * 48 * 192);
        float means[3] = {0.485f, 0.456f, 0.406f};
        float stds[3] = {0.229f, 0.224f, 0.225f};
        
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < 48 * 192; ++i) {
                int row = i / 192;
                int col = i % 192;
                float pixel_value = resized.at<cv::Vec3f>(row, col)[c];
                input_data[c * 48 * 192 + i] = (pixel_value - means[c]) / stds[c];
            }
        }
        
        // 设置输入
        auto input_names = cls_predictor_->GetInputNames();
        auto input_t = cls_predictor_->GetInputHandle(input_names[0]);
        input_t->Reshape({1, 3, 48, 192});
        input_t->CopyFromCpu(input_data.data());
        
        // 执行推理
        CHECK(cls_predictor_->Run());
        
        // 获取输出
        auto output_names = cls_predictor_->GetOutputNames();
        auto output_t = cls_predictor_->GetOutputHandle(output_names[0]);
        std::vector<float> output_data(2);  // 0度和180度两个类别
        output_t->CopyToCpu(output_data.data());
        
        // Softmax
        float exp_sum = std::exp(output_data[0]) + std::exp(output_data[1]);
        float prob_0 = std::exp(output_data[0]) / exp_sum;
        float prob_180 = std::exp(output_data[1]) / exp_sum;
        
        if (prob_0 > prob_180) {
            score = prob_0;
            return 0;
        } else {
            score = prob_180;
            return 180;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ClassifyAngle failed: " << e.what() << std::endl;
        score = 0.5f;
        return 0;  // 默认返回0度
    }
}

// 文本识别
std::pair<std::string, float> OcrEngine::recognize_text(cv::Mat& roi) {
    try {
        // 预处理
        cv::Mat resized;
        int target_h = 32;
        int target_w = static_cast<int>(roi.cols * target_h / static_cast<float>(roi.rows));
        target_w = std::min(target_w, 320);  // 限制最大宽度
        
        cv::resize(roi, resized, cv::Size(target_w, target_h));
        
        // 填充到固定宽度
        cv::Mat padded;
        if (target_w < 320) {
            cv::copyMakeBorder(resized, padded, 0, 0, 0, 320 - target_w, 
                              cv::BORDER_CONSTANT, cv::Scalar(0));
        } else {
            padded = resized;
        }
        
        padded.convertTo(padded, CV_32F, 1.0 / 255.0);
        
        // 归一化
        std::vector<float> input_data(3 * 32 * 320);
        float means[3] = {0.485f, 0.456f, 0.406f};
        float stds[3] = {0.229f, 0.224f, 0.225f};
        
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < 32 * 320; ++i) {
                int row = i / 320;
                int col = i % 320;
                float pixel_value = padded.at<cv::Vec3f>(row, col)[c];
                input_data[c * 32 * 320 + i] = (pixel_value - means[c]) / stds[c];
            }
        }
        
        // 设置输入
        auto input_names = rec_predictor_->GetInputNames();
        auto input_t = rec_predictor_->GetInputHandle(input_names[0]);
        input_t->Reshape({1, 3, 32, 320});
        input_t->CopyFromCpu(input_data.data());
        
        // 执行推理
        CHECK(rec_predictor_->Run());
        
        // 获取输出
        auto output_names = rec_predictor_->GetOutputNames();
        auto output_t = rec_predictor_->GetOutputHandle(output_names[0]);
        std::vector<int> output_shape = output_t->shape();
        
        int output_size = 1;
        for (int dim : output_shape) {
            output_size *= dim;
        }
        
        std::vector<float> output_data(output_size);
        output_t->CopyToCpu(output_data.data());
        
        // 后处理 - 简化版本
        // 在实际应用中，这里需要实现CTC解码和字符映射
        std::string recognized_text = "示例文本";
        float confidence = 0.85f;
        
        return {recognized_text, confidence};
        
    } catch (const std::exception& e) {
        std::cerr << "RecognizeText failed: " << e.what() << std::endl;
        return {"", 0.0f};
    }
}