#pragma once

/**
 * @file ocr_service.h
 * @brief 主OCR服务头文件，包含所有相关组件
 * 
 * 这个文件是OCR服务的主要接口，包含了所有必要的头文件
 * 和类声明。使用者只需要包含这个头文件即可使用所有OCR功能。
 */

// 包含所有分离的头文件
#include "ocr_worker.h"
#include "gpu_worker_pool.h"
#include "cpu_worker_pool.h"
#include "ocr_ipc_service.h"

namespace PaddleOCR {

// 所有类和结构体声明都在各自的头文件中
// 这里提供一个统一的入口点

} // namespace PaddleOCR
