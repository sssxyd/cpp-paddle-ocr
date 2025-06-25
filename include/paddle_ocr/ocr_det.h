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

namespace paddle_infer {
class Predictor;
}

namespace PaddleOCR {

class DBDetector {
public:
  /**
   * @brief 构造一个用于文本检测的 DBDetector 对象
   * 
   * 使用指定的配置参数初始化 DBDetector。
   * 检测器使用 PaddleOCR 的 DB (可微分二值化) 算法进行图像中的文本检测。
   * 
   * @param model_dir 包含推理模型文件的目录路径
   *                  (inference.pdmodel, inference.pdiparams, inference.yml)
   * @param use_gpu 是否使用 GPU 进行推理 (true) 或使用 CPU (false)
   * @param gpu_id 要使用的 GPU 设备 ID (仅在 use_gpu=true 时有效)
   * @param gpu_mem GPU 内存限制，单位 MB (仅在 use_gpu=true 时有效)
   * @param cpu_math_library_num_threads 数学库使用的 CPU 线程数
   * @param use_mkldnn 是否启用 Intel MKL-DNN 优化进行 CPU 推理
   * @param limit_type 图像缩放策略 ("max" 表示最大边限制, "min" 表示最小边限制)
   * @param limit_side_len 图像缩放的最大/最小边长 (像素)
   * @param det_db_thresh DB 后处理二值图的阈值 (0.0-1.0)
   * @param det_db_box_thresh 过滤检测框的阈值 (0.0-1.0)
   * @param det_db_unclip_ratio 扩展文本框的反裁剪区域比例 (>1.0)
   * @param det_db_score_mode 分数计算模式 ("slow" 表示精确, "fast" 表示快速)
   * @param use_dilation 是否应用膨胀形态学操作
   * @param use_tensorrt 是否启用 TensorRT 优化 (需要 TensorRT)
   * @param precision 推理精度 ("fp32", "fp16", "int8")
   * 
   * @throws std::runtime_error 如果模型加载失败或检测到不支持的模型
   * @throws YAML::Exception 如果 inference.yml 解析失败
   * 
   * @note 构造函数标记为 explicit 以防止隐式转换
   * @note 构造函数是 noexcept，但在关键错误时可能调用 std::exit()
   */
  explicit DBDetector(const std::string &model_dir, const bool &use_gpu,
                      const int &gpu_id, const int &gpu_mem,
                      const int &cpu_math_library_num_threads,
                      const bool &use_mkldnn, const std::string &limit_type,
                      const int &limit_side_len, const double &det_db_thresh,
                      const double &det_db_box_thresh,
                      const double &det_db_unclip_ratio,
                      const std::string &det_db_score_mode,
                      const bool &use_dilation, const bool &use_tensorrt,
                      const std::string &precision) noexcept {
    this->use_gpu_ = use_gpu;
    this->gpu_id_ = gpu_id;
    this->gpu_mem_ = gpu_mem;
    this->cpu_math_library_num_threads_ = cpu_math_library_num_threads;
    this->use_mkldnn_ = use_mkldnn;

    this->limit_type_ = limit_type;
    this->limit_side_len_ = limit_side_len;

    this->det_db_thresh_ = det_db_thresh;
    this->det_db_box_thresh_ = det_db_box_thresh;
    this->det_db_unclip_ratio_ = det_db_unclip_ratio;
    this->det_db_score_mode_ = det_db_score_mode;
    this->use_dilation_ = use_dilation;

    this->use_tensorrt_ = use_tensorrt;
    this->precision_ = precision;

    LoadModel(model_dir);
  }

  // Load Paddle inference model
  void LoadModel(const std::string &model_dir) noexcept;

  // Run predictor
  void Run(const cv::Mat &img,
           std::vector<std::vector<std::vector<int>>> &boxes,
           std::vector<double> &times) noexcept;

private:
  std::shared_ptr<paddle_infer::Predictor> predictor_;

  bool use_gpu_ = false;
  int gpu_id_ = 0;
  int gpu_mem_ = 4000;
  int cpu_math_library_num_threads_ = 4;
  bool use_mkldnn_ = false;

  std::string limit_type_ = "max";
  int limit_side_len_ = 960;

  double det_db_thresh_ = 0.3;
  double det_db_box_thresh_ = 0.5;
  double det_db_unclip_ratio_ = 2.0;
  std::string det_db_score_mode_ = "slow";
  bool use_dilation_ = false;

  bool visualize_ = true;
  bool use_tensorrt_ = false;
  std::string precision_ = "fp32";

  std::vector<float> mean_ = {0.485f, 0.456f, 0.406f};
  std::vector<float> scale_ = {1 / 0.229f, 1 / 0.224f, 1 / 0.225f};
  bool is_scale_ = true;

  // pre-process
  ResizeImgType0 resize_op_;
  Normalize normalize_op_;
  Permute permute_op_;

  // post-process
  DBPostProcessor post_processor_;
};

} // namespace PaddleOCR
