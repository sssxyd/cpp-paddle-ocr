#ifndef PADDLE_OCR_H
#define PADDLE_OCR_H

#include <vector>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include "paddle_inference/paddle_inference_api.h"

// 前向声明
namespace paddle_infer {
    class Predictor;
}

// OCR结果结构体
struct OcrResult {
    std::string text;
    float score;
    std::vector<int> rect;
};

// OCR引擎类
class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();
    
    // 初始化模型
    bool Init(const std::string& det_model_dir,
              const std::string& cls_model_dir, 
              const std::string& rec_model_dir);
    
    // 处理图像
    std::vector<OcrResult> Process(const unsigned char* gray_img_bytes, int width, int height);

private:
    // 模型创建函数
    std::shared_ptr<paddle_infer::Predictor> create_det_predictor();
    std::shared_ptr<paddle_infer::Predictor> create_cls_predictor();
    std::shared_ptr<paddle_infer::Predictor> create_rec_predictor();
    
    // 图像处理函数
    cv::Mat read_as_mat(const unsigned char* img_bytes, int w, int h);
    std::vector<std::vector<int>> detect_text(cv::Mat& roi, int w, int h);
    int classify_angle(cv::Mat& roi, float& score);
    std::pair<std::string, float> recognize_text(cv::Mat& roi);
    
    // 成员变量
    std::shared_ptr<paddle_infer::Predictor> det_predictor_;
    std::shared_ptr<paddle_infer::Predictor> cls_predictor_;
    std::shared_ptr<paddle_infer::Predictor> rec_predictor_;
    
    std::string det_model_dir_;
    std::string cls_model_dir_;
    std::string rec_model_dir_;
};

#endif // PADDLE_OCR_H