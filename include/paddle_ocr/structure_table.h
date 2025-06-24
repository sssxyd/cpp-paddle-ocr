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

class StructureTableRecognizer {
public:
  /**
   * @brief 构造一个用于表格结构识别的 StructureTableRecognizer 对象
   * 
   * 使用指定的配置参数初始化 StructureTableRecognizer。
   * 识别器基于深度学习模型，专门用于识别表格的结构信息，
   * 能够解析表格的行列布局、单元格合并关系，并输出 HTML 格式的表格结构。
   * 
   * @param model_dir 包含推理模型文件的目录路径
   *                  (inference.pdmodel, inference.pdiparams, inference.yml)
   * @param use_gpu 是否使用 GPU 进行推理 (true) 或使用 CPU (false)
   * @param gpu_id 要使用的 GPU 设备 ID (仅在 use_gpu=true 时有效)
   * @param gpu_mem GPU 内存限制，单位 MB (仅在 use_gpu=true 时有效)
   * @param cpu_math_library_num_threads 数学库使用的 CPU 线程数
   * @param use_mkldnn 是否启用 Intel MKL-DNN 优化进行 CPU 推理
   * @param label_path 表格结构标签文件路径，包含表格元素标记(如 <td>, <th>, <tr> 等)
   * @param use_tensorrt 是否启用 TensorRT 优化 (需要 TensorRT 和 GPU)
   * @param precision 推理精度 ("fp32", "fp16", "int8")
   * @param table_batch_num 批处理大小，同时处理的表格图像数量
   * @param table_max_len 表格序列的最大长度限制
   * @param merge_no_span_structure 是否合并无跨度的表格结构元素
   * 
   * @throws std::runtime_error 如果模型加载失败或标签文件读取失败
   * @throws YAML::Exception 如果 inference.yml 解析失败
   * @throws std::ios_base::failure 如果标签文件无法打开
   * 
   * @note 构造函数标记为 explicit 以防止隐式转换
   * @note 专门用于表格结构分析，输出 HTML 标签序列描述表格结构
   * @note 支持复杂表格，包括单元格合并(rowspan/colspan)的处理
   * @note 会自动初始化后处理器，设置结构合并参数
   * @note 如果 YAML 配置中包含字符字典，会自动生成新的标签文件
   * @note 构造函数是 noexcept，但在关键错误时可能调用 std::exit()
   */
  explicit StructureTableRecognizer(
      const std::string &model_dir, const bool &use_gpu, const int &gpu_id,
      const int &gpu_mem, const int &cpu_math_library_num_threads,
      const bool &use_mkldnn, const std::string &label_path,
      const bool &use_tensorrt, const std::string &precision,
      const int &table_batch_num, const int &table_max_len,
      const bool &merge_no_span_structure) noexcept {
    this->use_gpu_ = use_gpu;
    this->gpu_id_ = gpu_id;
    this->gpu_mem_ = gpu_mem;
    this->cpu_math_library_num_threads_ = cpu_math_library_num_threads;
    this->use_mkldnn_ = use_mkldnn;
    this->use_tensorrt_ = use_tensorrt;
    this->precision_ = precision;
    this->table_batch_num_ = table_batch_num;
    this->table_max_len_ = table_max_len;

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

    this->post_processor_.init(new_label_path, merge_no_span_structure);
    LoadModel(model_dir);
  }

  // Load Paddle inference model
  void LoadModel(const std::string &model_dir) noexcept;

  void Run(const std::vector<cv::Mat> &img_list,
           std::vector<std::vector<std::string>> &rec_html_tags,
           std::vector<float> &rec_scores,
           std::vector<std::vector<std::vector<int>>> &rec_boxes,
           std::vector<double> &times) noexcept;

private:
  std::shared_ptr<paddle_infer::Predictor> predictor_;

  bool use_gpu_ = false;
  int gpu_id_ = 0;
  int gpu_mem_ = 4000;
  int cpu_math_library_num_threads_ = 4;
  bool use_mkldnn_ = false;
  int table_max_len_ = 488;

  std::vector<float> mean_ = {0.485f, 0.456f, 0.406f};
  std::vector<float> scale_ = {1 / 0.229f, 1 / 0.224f, 1 / 0.225f};
  bool is_scale_ = true;

  bool use_tensorrt_ = false;
  std::string precision_ = "fp32";
  int table_batch_num_ = 1;

  // pre-process
  TableResizeImg resize_op_;
  Normalize normalize_op_;
  PermuteBatch permute_op_;
  TablePadImg pad_op_;

  // post-process
  TablePostProcessor post_processor_;

}; // class StructureTableRecognizer

} // namespace PaddleOCR
