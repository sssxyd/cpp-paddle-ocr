#ifndef AUTO_LOG_H
#define AUTO_LOG_H

#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

// 基于stdout的AutoLogger实现
class AutoLogger {
public:
    // 构造函数 - 支持完整的参数列表
    AutoLogger(const std::string& name, 
               bool use_gpu = false, 
               bool use_tensorrt = false,
               bool use_mkldnn = false, 
               int cpu_threads = 1,
               int batch_size = 1,
               const std::string& shape_info = "dynamic",
               const std::string& precision = "fp32",
               const std::vector<double>& time_info = {},
               int img_num = 1)
        : name_(name), 
          use_gpu_(use_gpu),
          use_tensorrt_(use_tensorrt),
          use_mkldnn_(use_mkldnn),
          cpu_threads_(cpu_threads),
          batch_size_(batch_size),
          shape_info_(shape_info),
          precision_(precision),
          time_info_(time_info),
          img_num_(img_num),
          start_time_(std::chrono::high_resolution_clock::now()) {
        
        // 启动时输出配置信息
        std::cout << "----------------------- Config Summary -----------------------" << std::endl;
        std::cout << "Model: " << name_ << std::endl;
        std::cout << "Use GPU: " << (use_gpu_ ? "True" : "False") << std::endl;
        std::cout << "Use TensorRT: " << (use_tensorrt_ ? "True" : "False") << std::endl;
        std::cout << "Use MKLDNN: " << (use_mkldnn_ ? "True" : "False") << std::endl;
        std::cout << "CPU Threads: " << cpu_threads_ << std::endl;
        std::cout << "Batch Size: " << batch_size_ << std::endl;
        std::cout << "Shape Info: " << shape_info_ << std::endl;
        std::cout << "Precision: " << precision_ << std::endl;
        std::cout << "Image Number: " << img_num_ << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;
    }
    
    ~AutoLogger() {
        // 析构时可以输出总结信息
    }
    
    // 报告性能统计信息
    void report() {
        if (time_info_.size() >= 3) {
            std::cout << "----------------------- " << name_ << " Summary -----------------------" << std::endl;
            std::cout << std::fixed << std::setprecision(2);
            
            double preprocess_time = time_info_[0];
            double inference_time = time_info_[1]; 
            double postprocess_time = time_info_[2];
            double total_time = preprocess_time + inference_time + postprocess_time;
            
            std::cout << "Preprocess time: " << preprocess_time << " ms" << std::endl;
            std::cout << "Inference time: " << inference_time << " ms" << std::endl;
            std::cout << "Postprocess time: " << postprocess_time << " ms" << std::endl;
            std::cout << "Total time: " << total_time << " ms" << std::endl;
            
            if (img_num_ > 0) {
                std::cout << "Average latency: " << total_time / img_num_ << " ms per image" << std::endl;
                std::cout << "QPS: " << 1000.0 * img_num_ / total_time << " images/sec" << std::endl;
            }
            
            // 时间占比分析
            if (total_time > 0) {
                std::cout << "Time breakdown:" << std::endl;
                std::cout << "  - Preprocess: " << (preprocess_time / total_time * 100) << "%" << std::endl;
                std::cout << "  - Inference: " << (inference_time / total_time * 100) << "%" << std::endl;
                std::cout << "  - Postprocess: " << (postprocess_time / total_time * 100) << "%" << std::endl;
            }
            
            std::cout << "------------------------------------------------------------" << std::endl;
        } else {
            // 如果没有时间信息，显示基本信息
            auto current_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time_);
            
            std::cout << "----------------------- " << name_ << " Summary -----------------------" << std::endl;
            std::cout << "Total elapsed time: " << duration.count() << " ms" << std::endl;
            std::cout << "------------------------------------------------------------" << std::endl;
        }
    }
    
    // 中间报告
    void info(const std::string& message) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time_);
        std::cout << "[" << name_ << "] " << message << " (elapsed: " << duration.count() << " ms)" << std::endl;
    }

private:
    std::string name_;
    bool use_gpu_;
    bool use_tensorrt_;
    bool use_mkldnn_;
    int cpu_threads_;
    int batch_size_;
    std::string shape_info_;
    std::string precision_;
    std::vector<double> time_info_;
    int img_num_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

#endif // AUTO_LOG_H
