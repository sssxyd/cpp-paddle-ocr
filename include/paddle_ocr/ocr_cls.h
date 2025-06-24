// Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <fstream>
#include <paddle_ocr/preprocess_op.h>
#include <paddle_ocr/utility.h>
#include <iostream>
#include <memory>
#include <yaml-cpp/yaml.h>

namespace paddle_infer {
class Predictor;
}

namespace PaddleOCR {

class Classifier {
public:
  /**
   * @brief 构造一个用于文本方向分类的 Classifier 对象
   * 
   * 使用指定的配置参数初始化 Classifier。
   * 分类器用于判断检测到的文本区域是否需要旋转（0度或180度），
   * 通常用于处理倒置的文本图像。
   * 
   * @param model_dir 包含推理模型文件的目录路径
   *                  (inference.pdmodel, inference.pdiparams, inference.yml)
   * @param use_gpu 是否使用 GPU 进行推理 (true) 或使用 CPU (false)
   * @param gpu_id 要使用的 GPU 设备 ID (仅在 use_gpu=true 时有效)
   * @param gpu_mem GPU 内存限制，单位 MB (仅在 use_gpu=true 时有效)
   * @param cpu_math_library_num_threads 数学库使用的 CPU 线程数
   * @param use_mkldnn 是否启用 Intel MKL-DNN 优化进行 CPU 推理
   * @param cls_thresh 分类置信度阈值 (0.0-1.0)，低于此值的结果将被忽略
   * @param use_tensorrt 是否启用 TensorRT 优化 (需要 TensorRT 和 GPU)
   * @param precision 推理精度 ("fp32", "fp16", "int8")
   * @param cls_batch_num 批处理大小，同时处理的图像数量
   * 
   * @throws std::runtime_error 如果模型加载失败或检测到不支持的模型
   * @throws YAML::Exception 如果 inference.yml 解析失败
   * 
   * @note 构造函数标记为 explicit 以防止隐式转换
   * @note 支持的模型："PP-LCNet_x0_25_textline_ori", "PP-LCNet_x1_0_textline_ori"
   * @note 构造函数是 noexcept，但在关键错误时可能调用 std::exit()
   */
  explicit Classifier(const std::string &model_dir, const bool &use_gpu,
                      const int &gpu_id, const int &gpu_mem,
                      const int &cpu_math_library_num_threads,
                      const bool &use_mkldnn, const double &cls_thresh,
                      const bool &use_tensorrt, const std::string &precision,
                      const int &cls_batch_num) noexcept {
    this->use_gpu_ = use_gpu;
    this->gpu_id_ = gpu_id;
    this->gpu_mem_ = gpu_mem;
    this->cpu_math_library_num_threads_ = cpu_math_library_num_threads;
    this->use_mkldnn_ = use_mkldnn;

    this->cls_thresh = cls_thresh;
    this->use_tensorrt_ = use_tensorrt;
    this->precision_ = precision;
    this->cls_batch_num_ = cls_batch_num;

    std::string yaml_file_path = model_dir + "/inference.yml";
    std::ifstream yaml_file(yaml_file_path);
    if (yaml_file.is_open()) {
      std::string model_name;
      try {
        YAML::Node config = YAML::LoadFile(yaml_file_path);
        if (config["Global"] && config["Global"]["model_name"]) {
          model_name = config["Global"]["model_name"].as<std::string>();
        }
        if (!model_name.empty() &&
            model_name != "PP-LCNet_x0_25_textline_ori" &&
            model_name != "PP-LCNet_x1_0_textline_ori") {
          std::cerr << "Error: " << model_name << " is currently not supported."
                    << std::endl;
          std::exit(EXIT_FAILURE);
        }
      } catch (const YAML::Exception &e) {
        std::cerr << "Failed to load YAML file: " << e.what() << std::endl;
      }
    }

    LoadModel(model_dir);
  }
  double cls_thresh = 0.9;

  // Load Paddle inference model
  void LoadModel(const std::string &model_dir) noexcept;

  void Run(const std::vector<cv::Mat> &img_list, std::vector<int> &cls_labels,
           std::vector<float> &cls_scores, std::vector<double> &times) noexcept;

private:
  std::shared_ptr<paddle_infer::Predictor> predictor_;

  bool use_gpu_ = false;
  int gpu_id_ = 0;
  int gpu_mem_ = 4000;
  int cpu_math_library_num_threads_ = 4;
  bool use_mkldnn_ = false;

  std::vector<float> mean_ = {0.5f, 0.5f, 0.5f};
  std::vector<float> scale_ = {1 / 0.5f, 1 / 0.5f, 1 / 0.5f};
  bool is_scale_ = true;
  bool use_tensorrt_ = false;
  std::string precision_ = "fp32";
  int cls_batch_num_ = 1;
  // pre-process
  ClsResizeImg resize_op_;
  Normalize normalize_op_;
  PermuteBatch permute_op_;

}; // class Classifier

} // namespace PaddleOCR
