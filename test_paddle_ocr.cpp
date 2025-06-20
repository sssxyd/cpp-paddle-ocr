#include "paddle_ocr.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <filesystem>

// 读取图像并转换为灰度字节数组
std::vector<unsigned char> LoadImageAsGrayBytes(const std::string& image_path, int& width, int& height) {
    std::cout << "正在读取图像: " << image_path << std::endl;
    
    // 检查文件是否存在
    if (!std::filesystem::exists(image_path)) {
        std::cerr << "图像文件不存在: " << image_path << std::endl;
        return {};
    }
    
    // 读取图像
    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "无法读取图像文件: " << image_path << std::endl;
        return {};
    }
    
    std::cout << "成功读取图像，原始尺寸: " << image.cols << "x" << image.rows << std::endl;
    
    // 转换为灰度图
    cv::Mat gray_image;
    cv::cvtColor(image, gray_image, cv::COLOR_BGR2GRAY);
    
    // 获取尺寸
    width = gray_image.cols;
    height = gray_image.rows;
    
    // 转换为字节数组
    std::vector<unsigned char> gray_bytes;
    
    if (gray_image.isContinuous()) {
        // 如果内存连续，直接复制
        gray_bytes.assign(gray_image.data, gray_image.data + gray_image.total());
    } else {
        // 如果内存不连续，逐行复制
        gray_bytes.reserve(width * height);
        for (int i = 0; i < height; ++i) {
            const unsigned char* row_ptr = gray_image.ptr<unsigned char>(i);
            gray_bytes.insert(gray_bytes.end(), row_ptr, row_ptr + width);
        }
    }
    
    std::cout << "转换为灰度字节数组，大小: " << gray_bytes.size() << " bytes" << std::endl;
    
    // 可选：保存预处理后的灰度图像以便调试
    cv::imwrite("debug_gray.jpg", gray_image);
    std::cout << "调试用灰度图像已保存为: debug_gray.jpg" << std::endl;
    
    return gray_bytes;
}

// 检查模型文件是否存在
bool CheckModelFiles(const std::string& base_dir) {
    std::vector<std::string> required_files = {
        base_dir + "/det/inference.pdmodel",
        base_dir + "/det/inference.pdiparams",
        base_dir + "/cls/inference.pdmodel", 
        base_dir + "/cls/inference.pdiparams",
        base_dir + "/rec/inference.pdmodel",
        base_dir + "/rec/inference.pdiparams"
    };
    
    std::cout << "检查模型文件..." << std::endl;
    bool all_exist = true;
    
    for (const auto& file : required_files) {
        if (std::filesystem::exists(file)) {
            std::cout << "Yes " << file << std::endl;
        } else {
            std::cerr << "Wrong " << file << " (文件不存在)" << std::endl;
            all_exist = false;
        }
    }
    
    return all_exist;
}

int main() {
    std::cout << "=== PaddleOCR 银行卡识别测试 ===" << std::endl;
    std::cout << "===================================" << std::endl;
    
    try {
        // 1. 检查图像文件
        std::string image_path = "images/card-jd.jpg";
        
        // 2. 检查模型文件
        std::string models_dir = "models";
        if (!CheckModelFiles(models_dir)) {
            std::cerr << "Wrong 模型文件检查失败，请确保模型文件已正确放置" << std::endl;
            return 1;
        }
        
        // 3. 读取并预处理图像
        int width, height;
        auto gray_bytes = LoadImageAsGrayBytes(image_path, width, height);
        
        if (gray_bytes.empty()) {
            std::cerr << "Wrong 图像读取失败" << std::endl;
            return 1;
        }
        
        // 4. 创建并初始化OCR引擎
        std::cout << "\n初始化OCR引擎..." << std::endl;
        OcrEngine engine;
        
        bool init_success = engine.Init(
            models_dir + "/det",
            models_dir + "/cls", 
            models_dir + "/rec"
        );
        
        if (!init_success) {
            std::cerr << "Wrong OCR引擎初始化失败" << std::endl;
            return 1;
        }
        
        std::cout << "Yes OCR引擎初始化成功" << std::endl;
        
        // 5. 执行OCR识别
        std::cout << "\n开始OCR识别..." << std::endl;
        std::cout << "图像尺寸: " << width << "x" << height << std::endl;
        std::cout << "数据大小: " << gray_bytes.size() << " bytes" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        auto results = engine.Process(gray_bytes.data(), width, height);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // 6. 输出结果
        std::cout << "\n=== OCR识别结果 ===" << std::endl;
        std::cout << "处理耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "检测到 " << results.size() << " 个文本区域" << std::endl;
        
        if (results.empty()) {
            std::cout << "Warn 未检测到任何文本" << std::endl;
        } else {
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& result = results[i];
                std::cout << "\n文本区域 " << (i + 1) << ":" << std::endl;
                std::cout << "  文本内容: \"" << result.text << "\"" << std::endl;
                std::cout << "  置信度: " << std::fixed << std::setprecision(3) << result.score << std::endl;
                
                if (result.rect.size() >= 4) {
                    std::cout << "  位置: (" << result.rect[0] << ", " << result.rect[1] 
                              << ") 大小: " << result.rect[2] << "x" << result.rect[3] << std::endl;
                }
            }
        }
        
        // 7. 额外信息
        std::cout << "\n=== 处理信息 ===" << std::endl;
        std::cout << "输入图像: " << image_path << std::endl;
        std::cout << "模型目录: " << models_dir << std::endl;
        std::cout << "调试文件: debug_gray.jpg (灰度图像)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Wrong 程序执行过程中出现异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n程序执行完成!" << std::endl;
    return 0;
}