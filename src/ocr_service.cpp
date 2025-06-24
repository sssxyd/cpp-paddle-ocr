#include "paddle_ocr/ocr_service.h"
#include <json/json.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace PaddleOCR {

// OCRWorker 实现
OCRWorker::OCRWorker(int worker_id, const std::string& model_dir, bool use_gpu, int gpu_id)
    : worker_id_(worker_id), use_gpu_(use_gpu), gpu_id_(gpu_id), running_(false), is_idle_(true) {
    
    try {
        // 初始化检测器
        detector_ = std::make_unique<DBDetector>(
            model_dir + "/det",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            "max", 960, 0.3, 0.5, 2.0, "slow", false,
            use_gpu, "fp32"
        );
        
        // 初始化分类器
        classifier_ = std::make_unique<Classifier>(
            model_dir + "/cls",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            0.9, use_gpu, "fp32", 1
        );
        
        // 初始化识别器
        recognizer_ = std::make_unique<CRNNRecognizer>(
            model_dir + "/rec",
            use_gpu, gpu_id, 4000, use_gpu ? 1 : 2, !use_gpu,
            model_dir + "/rec/ppocr_keys_v1.txt",
            use_gpu, "fp32", 6, 48, 320
        );
        
        std::cout << "OCRWorker " << worker_id_ << " initialized successfully (" 
                  << (use_gpu ? "GPU" : "CPU") << ")" << std::endl;
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
                json_result["success"] = result.success;
                json_result["processing_time_ms"] = result.processing_time_ms;
                json_result["worker_id"] = worker_id_;
                
                if (result.success) {
                    Json::Value texts_array(Json::arrayValue);
                    for (const auto& text : result.texts) {
                        texts_array.append(text);
                    }
                    json_result["texts"] = texts_array;
                    
                    Json::Value boxes_array(Json::arrayValue);
                    for (const auto& box : result.boxes) {
                        Json::Value box_array(Json::arrayValue);
                        for (const auto& point : box) {
                            Json::Value point_array(Json::arrayValue);
                            point_array.append(point[0]);
                            point_array.append(point[1]);
                            box_array.append(point_array);
                        }
                        boxes_array.append(box_array);
                    }
                    json_result["boxes"] = boxes_array;
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
        cv::Mat image;
        
        // 加载图像
        if (!request.image_path.empty()) {
            image = cv::imread(request.image_path);
            if (image.empty()) {
                result.error_message = "Failed to load image: " + request.image_path;
                return result;
            }
        } else if (!request.image_data.empty()) {
            image = request.image_data;
        } else {
            result.error_message = "No image data provided";
            return result;
        }
        
        // 文本检测
        std::vector<std::vector<std::vector<int>>> det_boxes;
        std::vector<double> det_times;
        detector_->Run(image, det_boxes, det_times);
        
        if (det_boxes.empty()) {
            result.success = true;
            result.texts = {};
            result.boxes = {};
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
            result.texts = {};
            result.boxes = {};
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
        
        // 文本方向分类
        std::vector<int> cls_labels;
        std::vector<float> cls_scores;
        std::vector<double> cls_times;
        classifier_->Run(text_images, cls_labels, cls_scores, cls_times);
        
        // 根据分类结果旋转图像
        for (size_t i = 0; i < text_images.size() && i < cls_labels.size(); ++i) {
            if (cls_labels[i] == 1) {  // 需要旋转180度
                cv::rotate(text_images[i], text_images[i], cv::ROTATE_180);
            }
        }
        
        // 文本识别
        std::vector<std::string> rec_texts;
        std::vector<float> rec_scores;
        std::vector<double> rec_times;
        recognizer_->Run(text_images, rec_texts, rec_scores, rec_times);
        
        // 设置结果
        result.success = true;
        result.texts = rec_texts;
        result.boxes = det_boxes;
        result.confidences = rec_scores;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
    catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

// GPUWorkerPool 实现
GPUWorkerPool::GPUWorkerPool(const std::string& model_dir, int num_workers) 
    : next_worker_index_(0), gpu_memory_per_worker_mb_(1500) {
    
    // 检查GPU内存是否足够
    if (!checkGPUMemory(num_workers)) {
        num_workers = std::max(1, num_workers / 2);  // 减少Worker数量
        std::cout << "GPU memory limited, reducing workers to: " << num_workers << std::endl;
    }
    
    workers_.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(std::make_unique<OCRWorker>(
            i, model_dir, true, 0  // 所有Worker使用GPU 0
        ));
    }
    
    std::cout << "GPUWorkerPool created with " << num_workers << " workers" << std::endl;
}

GPUWorkerPool::~GPUWorkerPool() {
    stop();
}

void GPUWorkerPool::start() {
    for (auto& worker : workers_) {
        worker->start();
    }
}

void GPUWorkerPool::stop() {
    for (auto& worker : workers_) {
        worker->stop();
    }
}

std::future<std::string> GPUWorkerPool::submitRequest(std::shared_ptr<OCRRequest> request) {
    auto future = request->result_promise.get_future();
    
    OCRWorker* worker = getAvailableWorker();
    worker->addRequest(request);
    
    return future;
}

OCRWorker* GPUWorkerPool::getAvailableWorker() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    
    // 首先尝试找空闲的worker
    for (auto& worker : workers_) {
        if (worker->isIdle()) {
            return worker.get();
        }
    }
    
    // 如果没有空闲的，轮询分配
    int index = next_worker_index_.fetch_add(1) % workers_.size();
    return workers_[index].get();
}

int GPUWorkerPool::getOptimalWorkerCount() {
    // 根据GPU内存自动计算最优Worker数量
    // 每个Worker大约需要1.5GB GPU内存
    int max_workers = gpu_memory_per_worker_mb_ > 0 ? 
        (8000 / gpu_memory_per_worker_mb_) : 2;  // 假设8GB GPU
    
    return std::min(4, std::max(1, max_workers));  // 限制在1-4个Worker之间
}

bool GPUWorkerPool::checkGPUMemory(int num_workers) {
    // 简化版本：假设内存总是足够
    // 实际部署时可根据具体情况调整
    int required_memory_mb = num_workers * gpu_memory_per_worker_mb_;
    std::cout << "GPU Memory Required: " << required_memory_mb << "MB (assuming sufficient)" << std::endl;
    return true;
}

// CPUWorkerPool 实现
CPUWorkerPool::CPUWorkerPool(const std::string& model_dir, int num_workers) 
    : next_worker_index_(0) {
    
    workers_.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(std::make_unique<OCRWorker>(i, model_dir, false));
    }
    
    std::cout << "CPUWorkerPool created with " << num_workers << " workers" << std::endl;
}

CPUWorkerPool::~CPUWorkerPool() {
    stop();
}

void CPUWorkerPool::start() {
    for (auto& worker : workers_) {
        worker->start();
    }
}

void CPUWorkerPool::stop() {
    for (auto& worker : workers_) {
        worker->stop();
    }
}

std::future<std::string> CPUWorkerPool::submitRequest(std::shared_ptr<OCRRequest> request) {
    auto future = request->result_promise.get_future();
    
    OCRWorker* worker = getAvailableWorker();
    worker->addRequest(request);
    
    return future;
}

OCRWorker* CPUWorkerPool::getAvailableWorker() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    
    // 首先尝试找空闲的worker
    for (auto& worker : workers_) {
        if (worker->isIdle()) {
            return worker.get();
        }
    }
    
    // 如果没有空闲的，轮询分配
    int index = next_worker_index_.fetch_add(1) % workers_.size();
    return workers_[index].get();
}

// OCRIPCService 实现
OCRIPCService::OCRIPCService(const std::string& model_dir, const std::string& pipe_name, 
                           bool force_cpu, int gpu_workers, int cpu_workers)
    : model_dir_(model_dir), pipe_name_(pipe_name), force_cpu_(force_cpu), 
      gpu_workers_(gpu_workers), cpu_workers_(cpu_workers), running_(false), request_counter_(0), 
      total_requests_(0), successful_requests_(0), total_processing_time_(0.0) {
    
    // 检测硬件
    has_gpu_ = !force_cpu && detectGPU();
    cpu_cores_ = getCPUCoreCount();
    gpu_memory_mb_ = getGPUMemory();
    
    std::cout << "OCR Service Configuration:" << std::endl;
    std::cout << "  Model Directory: " << model_dir_ << std::endl;
    std::cout << "  Pipe Name: " << pipe_name_ << std::endl;
    std::cout << "  GPU Available: " << (has_gpu_ ? "Yes" : "No") << std::endl;
    std::cout << "  CPU Cores: " << cpu_cores_ << std::endl;
      // 初始化worker
    if (has_gpu_) {
        // 使用指定的GPU Worker数量
        gpu_worker_pool_ = std::make_unique<GPUWorkerPool>(model_dir_, gpu_workers_);
        std::cout << "  Mode: GPU (" << gpu_workers_ << " Workers)" << std::endl;
        std::cout << "  GPU Memory: " << gpu_memory_mb_ << " MB" << std::endl;
    } else {
        // 使用指定的CPU Worker数量
        cpu_worker_pool_ = std::make_unique<CPUWorkerPool>(model_dir_, cpu_workers_);
        std::cout << "  Mode: CPU (" << cpu_workers_ << " Workers)" << std::endl;
    }
}

OCRIPCService::~OCRIPCService() {
    stop();
}

bool OCRIPCService::start() {
    if (running_) return true;
    
    try {        // 启动worker
        if (has_gpu_) {
            gpu_worker_pool_->start();
        } else {
            cpu_worker_pool_->start();
        }
        
        running_ = true;
        ipc_thread_ = std::thread(&OCRIPCService::ipcServerLoop, this);
        
        std::cout << "OCR IPC Service started successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to start OCR IPC Service: " << e.what() << std::endl;
        return false;
    }
}

void OCRIPCService::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 停止IPC线程
    if (ipc_thread_.joinable()) {
        ipc_thread_.join();
    }
    
    // 等待客户端线程结束
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_);
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
      // 停止worker
    if (has_gpu_) {
        gpu_worker_pool_->stop();
    } else {
        cpu_worker_pool_->stop();
    }
    
    std::cout << "OCR IPC Service stopped" << std::endl;
}

void OCRIPCService::ipcServerLoop() {
    while (running_) {
        HANDLE pipe_handle = CreateNamedPipeA(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536,  // output buffer size
            65536,  // input buffer size
            0,      // default timeout
            NULL    // default security attributes
        );
        
        if (pipe_handle == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create named pipe: " << GetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        if (ConnectNamedPipe(pipe_handle, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            // 在新线程中处理客户端连接
            {
                std::lock_guard<std::mutex> lock(client_threads_mutex_);
                client_threads_.emplace_back([this, pipe_handle]() {
                    handleClientConnection(pipe_handle);
                });
            }
        } else {
            CloseHandle(pipe_handle);
        }
    }
}

void OCRIPCService::handleClientConnection(HANDLE pipe_handle) {
    const int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    while (running_) {
        if (ReadFile(pipe_handle, buffer, BUFFER_SIZE - 1, &bytes_read, NULL)) {
            buffer[bytes_read] = '\0';
            std::string request(buffer);
            
            std::string response = processIPCRequest(request);
            
            DWORD bytes_written;
            WriteFile(pipe_handle, response.c_str(), response.length(), &bytes_written, NULL);
        } else {
            break;  // 客户端断开连接
        }
    }
    
    DisconnectNamedPipe(pipe_handle);
    CloseHandle(pipe_handle);
}

std::string OCRIPCService::processIPCRequest(const std::string& request_json) {
    try {
        Json::Value request;
        Json::CharReaderBuilder builder;
        std::istringstream stream(request_json);
        std::string errors;
        
        if (!Json::parseFromStream(builder, stream, &request, &errors)) {
            Json::Value error_response;
            error_response["success"] = false;
            error_response["error"] = "Invalid JSON: " + errors;
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, error_response);
        }
        
        std::string command = request.get("command", "").asString();
        
        if (command == "recognize") {
            std::string image_path = request.get("image_path", "").asString();
            
            if (image_path.empty()) {
                Json::Value error_response;
                error_response["success"] = false;
                error_response["error"] = "Missing image_path";
                Json::StreamWriterBuilder writer_builder;
                return Json::writeString(writer_builder, error_response);
            }
            
            auto future = processOCRRequest(image_path);
            return future.get();
        }
        else if (command == "status") {
            Json::Value status_response;
            status_response["success"] = true;
            status_response["status"] = getStatusInfo();
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, status_response);
        }
        else {
            Json::Value error_response;
            error_response["success"] = false;
            error_response["error"] = "Unknown command: " + command;
            Json::StreamWriterBuilder writer_builder;
            return Json::writeString(writer_builder, error_response);
        }
    }
    catch (const std::exception& e) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = e.what();
        Json::StreamWriterBuilder writer_builder;
        return Json::writeString(writer_builder, error_response);
    }
}

std::future<std::string> OCRIPCService::processOCRRequest(const std::string& image_path) {
    int request_id = request_counter_.fetch_add(1);
    auto request = std::make_shared<OCRRequest>(request_id, image_path);
    
    total_requests_.fetch_add(1);
    
    if (has_gpu_) {
        return gpu_worker_pool_->submitRequest(request);
    } else {
        return cpu_worker_pool_->submitRequest(request);
    }
}

std::future<std::string> OCRIPCService::processOCRRequest(const cv::Mat& image) {
    int request_id = request_counter_.fetch_add(1);
    auto request = std::make_shared<OCRRequest>(request_id, image);
    
    total_requests_.fetch_add(1);
    
    if (has_gpu_) {
        return gpu_worker_pool_->submitRequest(request);
    } else {
        return cpu_worker_pool_->submitRequest(request);
    }
}

bool OCRIPCService::detectGPU() {
    // 简化版本：通过命令行参数或环境变量指定是否使用GPU
    // 实际部署时可根据具体情况判断GPU可用性
    std::cout << "GPU detection simplified - use --use-gpu flag to enable" << std::endl;
    return false;  // 默认使用CPU模式
}

int OCRIPCService::getCPUCoreCount() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

int OCRIPCService::getGPUMemory() {
    // 简化版本：返回默认值
    // 实际部署时可根据具体GPU型号设置合适的值
    std::cout << "GPU Memory: assuming 4GB default" << std::endl;
    return 4096;  // 默认假设4GB
}

std::string OCRIPCService::getStatusInfo() const {
    Json::Value status;
    status["running"] = running_.load();
    status["has_gpu"] = has_gpu_;
    status["cpu_cores"] = cpu_cores_;
    status["total_requests"] = total_requests_.load();
    status["successful_requests"] = successful_requests_.load();
    status["average_processing_time_ms"] = total_requests_.load() > 0 ? 
        total_processing_time_.load() / total_requests_.load() : 0.0;
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, status);
}

// OCRIPCClient 实现
OCRIPCClient::OCRIPCClient(const std::string& pipe_name) 
    : pipe_name_(pipe_name), pipe_handle_(INVALID_HANDLE_VALUE), connected_(false) {
}

OCRIPCClient::~OCRIPCClient() {
    disconnect();
}

bool OCRIPCClient::connect(int timeout_ms) {
    if (connected_) return true;
    
    DWORD start_time = GetTickCount();
    
    while (GetTickCount() - start_time < timeout_ms) {
        pipe_handle_ = CreateFileA(
            pipe_name_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            connected_ = true;
            return true;
        }
        
        if (GetLastError() != ERROR_PIPE_BUSY) {
            break;
        }
        
        if (!WaitNamedPipeA(pipe_name_.c_str(), 1000)) {
            break;
        }
    }
    
    return false;
}

void OCRIPCClient::disconnect() {
    if (connected_) {
        CloseHandle(pipe_handle_);
        pipe_handle_ = INVALID_HANDLE_VALUE;
        connected_ = false;
    }
}

std::string OCRIPCClient::recognizeImage(const std::string& image_path) {
    Json::Value request;
    request["command"] = "recognize";
    request["image_path"] = image_path;
    
    Json::StreamWriterBuilder builder;
    return sendRequest(Json::writeString(builder, request));
}

std::string OCRIPCClient::recognizeImage(const cv::Mat& image) {
    // 将图像编码为base64
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    
    // 这里简化处理，实际应该将图像数据编码为base64传输
    // 或者先保存到临时文件再传输路径
    std::string temp_path = "temp_" + std::to_string(GetTickCount()) + ".jpg";
    cv::imwrite(temp_path, image);
    
    auto result = recognizeImage(temp_path);
    
    // 删除临时文件
    DeleteFileA(temp_path.c_str());
    
    return result;
}

std::string OCRIPCClient::sendRequest(const std::string& request_json) {
    if (!connected_) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Not connected to service";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
    
    std::lock_guard<std::mutex> lock(comm_mutex_);
    
    // 发送请求
    DWORD bytes_written;
    if (!WriteFile(pipe_handle_, request_json.c_str(), request_json.length(), &bytes_written, NULL)) {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to send request";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
    
    // 读取响应
    const int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    if (ReadFile(pipe_handle_, buffer, BUFFER_SIZE - 1, &bytes_read, NULL)) {
        buffer[bytes_read] = '\0';
        return std::string(buffer);
    } else {
        Json::Value error_response;
        error_response["success"] = false;
        error_response["error"] = "Failed to read response";
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, error_response);
    }
}

} // namespace PaddleOCR
