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
#include <paddle_ocr/postprocess_op.h>
#include <paddle_ocr/preprocess_op.h>
#include <iostream>
#include <memory>
#include <yaml-cpp/yaml.h>

namespace paddle_infer {
class Predictor;
}

namespace PaddleOCR {

class StructureLayoutRecognizer {
public:
  /**
   * @brief 构造一个用于文档结构布局分析的 StructureLayoutRecognizer 对象
   * 
   * 使用指定的配置参数初始化 StructureLayoutRecognizer。
   * 识别器基于 PicoDet 目标检测算法，用于分析文档的版面结构，
   * 能够识别文本区域、标题、图片、表格等不同的布局元素。
   * 
   * @param model_dir 包含推理模型文件的目录路径
   *                  (inference.pdmodel, inference.pdiparams, inference.yml)
   * @param use_gpu 是否使用 GPU 进行推理 (true) 或使用 CPU (false)
   * @param gpu_id 要使用的 GPU 设备 ID (仅在 use_gpu=true 时有效)
   * @param gpu_mem GPU 内存限制，单位 MB (仅在 use_gpu=true 时有效)
   * @param cpu_math_library_num_threads 数学库使用的 CPU 线程数
   * @param use_mkldnn 是否启用 Intel MKL-DNN 优化进行 CPU 推理
   * @param label_path 布局类别标签文件路径，包含可识别的布局元素类型
   * @param use_tensorrt 是否启用 TensorRT 优化 (需要 TensorRT 和 GPU)
   * @param precision 推理精度 ("fp32", "fp16", "int8")
   * @param layout_score_threshold 布局检测的置信度阈值 (0.0-1.0)
   * @param layout_nms_threshold 非极大值抑制(NMS)的 IoU 阈值 (0.0-1.0)
   * 
   * @throws std::runtime_error 如果模型加载失败或标签文件读取失败
   * @throws YAML::Exception 如果 inference.yml 解析失败
   * @throws std::ios_base::failure 如果标签文件无法打开
   * 
   * @note 构造函数标记为 explicit 以防止隐式转换
   * @note 基于 PicoDet 轻量级目标检测网络，适合移动端和边缘设备
   * @note 会自动初始化后处理器，设置检测阈值和 NMS 参数
   * @note 如果 YAML 配置中包含字符字典，会自动生成新的标签文件
   * @note 构造函数是 noexcept，但在关键错误时可能调用 std::exit()
   */
  explicit StructureLayoutRecognizer(
      const std::string &model_dir, const bool &use_gpu, const int &gpu_id,
      const int &gpu_mem, const int &cpu_math_library_num_threads,
      const bool &use_mkldnn, const std::string &label_path,
      const bool &use_tensorrt, const std::string &precision,
      const double &layout_score_threshold,
      const double &layout_nms_threshold) noexcept {
    this->use_gpu_ = use_gpu;
    this->gpu_id_ = gpu_id;
    this->gpu_mem_ = gpu_mem;
    this->cpu_math_library_num_threads_ = cpu_math_library_num_threads;
    this->use_mkldnn_ = use_mkldnn;
    this->use_tensorrt_ = use_tensorrt;
    this->precision_ = precision;

    std::string new_label_path = label_path;
    std::string yaml_file_path = model_dir + "/inference.yml";
    std::ifstream yaml_file(yaml_file_path);
    if (yaml_file.is_open()) {
      std::string model_name;
      std::vector<std::string> rec_char_list;
      try {
        YAML::Node config = YAML::LoadFile(yaml_file_path);
        if (config["Global"] && config["Global"]["model_name"]) {
          model_name = config["Global"]["model_name"].as<std::string>();
        }
        if (!model_name.empty()) {
          std::cerr << "Error: " << model_name << " is currently not supported."
                    << std::endl;
          std::exit(EXIT_FAILURE);
        }
        if (config["PostProcess"] && config["PostProcess"]["character_dict"]) {
          rec_char_list = config["PostProcess"]["character_dict"]
                              .as<std::vector<std::string>>();
        }
      } catch (const YAML::Exception &e) {
        std::cerr << "Failed to load YAML file: " << e.what() << std::endl;
      }
      if (label_path == "../../ppocr/utils/ppocr_keys_v1.txt" &&
          !rec_char_list.empty()) {
        std::string new_rec_char_dict_path = model_dir + "/ppocr_keys.txt";
        std::ofstream new_file(new_rec_char_dict_path);
        if (new_file.is_open()) {
          for (const auto &character : rec_char_list) {
            new_file << character << '\n';
          }
          new_label_path = new_rec_char_dict_path;
        }
      }
    }

    this->post_processor_.init(new_label_path, layout_score_threshold,
                               layout_nms_threshold);
    LoadModel(model_dir);
  }

  // Load Paddle inference model
  void LoadModel(const std::string &model_dir) noexcept;

  void Run(const cv::Mat &img, std::vector<StructurePredictResult> &result,
           std::vector<double> &times) noexcept;

private:
  std::shared_ptr<paddle_infer::Predictor> predictor_;

  bool use_gpu_ = false;
  int gpu_id_ = 0;
  int gpu_mem_ = 4000;
  int cpu_math_library_num_threads_ = 4;
  bool use_mkldnn_ = false;

  std::vector<float> mean_ = {0.485f, 0.456f, 0.406f};
  std::vector<float> scale_ = {1 / 0.229f, 1 / 0.224f, 1 / 0.225f};
  bool is_scale_ = true;

  bool use_tensorrt_ = false;
  std::string precision_ = "fp32";

  // pre-process
  Resize resize_op_;
  Normalize normalize_op_;
  Permute permute_op_;

  // post-process
  PicodetPostProcessor post_processor_;
};

} // namespace PaddleOCR
