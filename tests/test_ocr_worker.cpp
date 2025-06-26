#include <iostream>
#include <opencv2/opencv.hpp>
#include <memory>
#include <future>
#include <thread>
#include <chrono>
#include <json/json.h>
#include <fstream>
#include <cassert>
#include <filesystem>

#include <paddle_ocr/ocr_worker.h>
#include "simple_test.h"

using namespace PaddleOCR;

/**
 * @brief OCRWorker 测试类
 */
class OCRWorkerTest {
private:
    std::string model_dir_;
    cv::Mat test_image_;
    cv::Mat empty_image_;
    cv::Mat small_image_;
    std::unique_ptr<OCRWorker> worker_;
    
public:
    void setUp() {
        model_dir_ = "models";
        test_image_ = createTestImage();
        empty_image_ = cv::Mat();
        small_image_ = cv::Mat::zeros(10, 10, CV_8UC3);
    }
    
    void tearDown() {
        if (worker_ && worker_->getWorkerId() >= 0) {
            worker_->stop();
        }
        worker_.reset();
    }
    
    /**
     * @brief 显示系统信息和Worker建议
     */
    void showSystemInfo() {
        SimpleTest::printLine("=== 系统信息和Worker配置建议 ===");
        
        // GPU模式建议
        std::string gpu_recommendation = OCRWorker::getWorkerRecommendation(true, false);
        SimpleTest::printLine("\n--- GPU模式 (无分类器) ---");
        std::cout << gpu_recommendation << std::endl;
        
        // GPU模式 + 分类器建议  
        std::string gpu_cls_recommendation = OCRWorker::getWorkerRecommendation(true, true);
        SimpleTest::printLine("\n--- GPU模式 (有分类器) ---");
        std::cout << gpu_cls_recommendation << std::endl;
        
        // CPU模式建议
        std::string cpu_recommendation = OCRWorker::getWorkerRecommendation(false, false);
        SimpleTest::printLine("\n--- CPU模式 (无分类器) ---");
        std::cout << cpu_recommendation << std::endl;
        
        // CPU模式 + 分类器建议
        std::string cpu_cls_recommendation = OCRWorker::getWorkerRecommendation(false, true);
        SimpleTest::printLine("\n--- CPU模式 (有分类器) ---");
        std::cout << cpu_cls_recommendation << std::endl;
    }
    
    cv::Mat createTestImage() {
        cv::Mat image = cv::Mat::zeros(200, 600, CV_8UC3);
        image.setTo(cv::Scalar(255, 255, 255)); // 白色背景
        
        // 添加一些文本
        cv::putText(image, "Hello OCR Test", cv::Point(50, 50), 
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
        cv::putText(image, "PaddleOCR", cv::Point(50, 100), 
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
        cv::putText(image, "Test Worker", cv::Point(50, 150), 
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);
        
        return image;
    }
    
    cv::Mat loadTestImageFromFile(const std::string& filename) {
        std::string filepath = "images/" + filename;
        cv::Mat image = cv::imread(filepath);
        if (image.empty()) {
            return createTestImage();
        }
        return image;
    }
    
    Json::Value parseJsonResult(const std::string& json_str) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(json_str);
        
        if (!Json::parseFromStream(builder, iss, &root, &errors)) {
            throw std::runtime_error("Failed to parse JSON: " + errors);
        }
        
        return root;
    }
    
    void testConstructorCPU() {
        SimpleTest::printLine("\n=== 测试 OCRWorker 构造函数 (CPU) ===");
        
        SimpleTest::expectNoThrow([this]() {
            worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false); // 默认关闭文本方向分类
        }, "OCRWorker constructor should not throw");
        
        SimpleTest::assertNotNull(worker_.get(), "Worker should not be null");
        SimpleTest::assertEquals(1, worker_->getWorkerId(), "Worker ID should be 1");
        SimpleTest::assertTrue(worker_->isIdle(), "Worker should be idle initially");
    }
    
    void testStartStop() {
        SimpleTest::printLine("\n=== 测试 OCRWorker 启动/停止 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        
        SimpleTest::expectNoThrow([this]() {
            worker_->start();
        }, "Worker start should not throw");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        SimpleTest::expectNoThrow([this]() {
            worker_->stop();
        }, "Worker stop should not throw");
        
        SimpleTest::expectNoThrow([this]() {
            worker_->stop();
        }, "Multiple stop calls should not throw");
    }
    
    void testMultipleStart() {
        SimpleTest::printLine("\n=== 测试多次启动调用 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        
        SimpleTest::expectNoThrow([this]() {
            worker_->start();
            worker_->start();
        }, "Multiple start calls should not throw");
        
        worker_->stop();
    }
    
    void testBasicOCRProcessing() {
        SimpleTest::printLine("\n=== 测试基本 OCR 处理 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        worker_->start();
        
        auto request = std::make_shared<OCRRequest>(1001, test_image_);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "OCR processing should complete within 30 seconds");
        
        std::string result_json = future.get();
        SimpleTest::assertTrue(!result_json.empty(), "Result JSON should not be empty");
        
        Json::Value result = parseJsonResult(result_json);
        
        // 打印人类可读的JSON结果
        SimpleTest::printJsonResult(result, "基本OCR处理结果");
        
        SimpleTest::assertEquals(1001, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertEquals(1, result["worker_id"].asInt(), "Worker ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "OCR should succeed");
        SimpleTest::assertTrue(result["processing_time_ms"].asDouble() > 0.0, "Processing time should be positive");
        
        worker_->stop();
    }
    
    void testRealImageProcessing() {
        SimpleTest::printLine("\n=== 测试真实图像处理 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        worker_->start();
        
        cv::Mat real_image = loadTestImageFromFile("card-jd.jpg");
        
        auto request = std::make_shared<OCRRequest>(1002, real_image);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "Real image processing should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        // 打印人类可读的JSON结果
        SimpleTest::printJsonResult(result, "真实图像处理结果");
        
        SimpleTest::assertEquals(1002, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "Real image OCR should succeed");
        
        if (result["success"].asBool()) {
            SimpleTest::assertTrue(result.isMember("texts"), "Result should contain texts");
            SimpleTest::assertTrue(result.isMember("boxes"), "Result should contain boxes");
            
            SimpleTest::printLine("OCR 识别结果:");
            const Json::Value& texts = result["texts"];
            for (const auto& text : texts) {
                SimpleTest::printLine("  - " + text.asString());
            }
        }
        
        worker_->stop();
    }
    
    void testEmptyImageProcessing() {
        SimpleTest::printLine("\n=== 测试空图像处理 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        worker_->start();
        
        auto request = std::make_shared<OCRRequest>(1003, empty_image_);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(10));
        SimpleTest::assertTrue(status == std::future_status::ready, "Empty image processing should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        // 打印人类可读的JSON结果
        SimpleTest::printJsonResult(result, "空图像处理结果");
        
        SimpleTest::assertEquals(1003, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertFalse(result["success"].asBool(), "Empty image should fail");
        SimpleTest::assertTrue(result.isMember("error"), "Result should contain error message");
        
        worker_->stop();
    }
    
    void testConcurrentProcessing() {
        SimpleTest::printLine("\n=== 测试并发处理 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        worker_->start();
        
        const int num_requests = 3;
        std::vector<std::shared_ptr<OCRRequest>> requests;
        std::vector<std::future<std::string>> futures;
        
        for (int i = 0; i < num_requests; ++i) {
            auto request = std::make_shared<OCRRequest>(2000 + i, test_image_);
            auto future = request->result_promise.get_future();
            
            requests.push_back(request);
            futures.push_back(std::move(future));
            
            worker_->addRequest(request);
        }
        
        for (int i = 0; i < num_requests; ++i) {
            auto status = futures[i].wait_for(std::chrono::seconds(60));
            SimpleTest::assertTrue(status == std::future_status::ready, 
                                  "Concurrent request " + std::to_string(i) + " should complete");
            
            std::string result_json = futures[i].get();
            Json::Value result = parseJsonResult(result_json);
            
            SimpleTest::assertEquals(2000 + i, result["request_id"].asInt(), 
                                   "Request ID should match for request " + std::to_string(i));
            SimpleTest::assertEquals(1, result["worker_id"].asInt(), "Worker ID should match");
        }
        
        worker_->stop();
    }
    
    void testIdleState() {
        SimpleTest::printLine("\n=== 测试空闲状态 ===");
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0, false);
        
        SimpleTest::assertTrue(worker_->isIdle(), "Worker should be idle before start");
        
        worker_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SimpleTest::assertTrue(worker_->isIdle(), "Worker should be idle after start with no tasks");
        
        auto request = std::make_shared<OCRRequest>(3001, test_image_);
        auto future = request->result_promise.get_future();
        worker_->addRequest(request);
        
        future.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SimpleTest::assertTrue(worker_->isIdle(), "Worker should be idle after task completion");
        
        worker_->stop();
    }
    
    void testInvalidModelPath() {
        SimpleTest::printLine("\n=== 测试无效模型路径 ===");
        
        SimpleTest::expectThrow([this]() {
            auto invalid_worker = std::make_unique<OCRWorker>(1, "invalid_model_path", false, 0, false);
        }, "Invalid model path should throw exception");
    }
    
    void testWithTextClassification() {
        SimpleTest::printLine("\n=== 测试启用文本方向分类 ===");
        
        // 创建启用文本方向分类的worker
        auto cls_worker = std::make_unique<OCRWorker>(2, model_dir_, false, 0, true);
        cls_worker->start();
        
        auto request = std::make_shared<OCRRequest>(2001, test_image_);
        auto future = request->result_promise.get_future();
        
        cls_worker->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "OCR with classification should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        // 打印启用文本分类的结果
        SimpleTest::printJsonResult(result, "启用文本方向分类结果");
        
        SimpleTest::assertEquals(2001, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertEquals(2, result["worker_id"].asInt(), "Worker ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "OCR with classification should succeed");
        
        cls_worker->stop();
    }
    
    void testWithoutTextClassification() {
        SimpleTest::printLine("\n=== 测试禁用文本方向分类 ===");
        
        // 创建禁用文本方向分类的worker（默认情况）
        auto no_cls_worker = std::make_unique<OCRWorker>(3, model_dir_, false, 0, false);
        no_cls_worker->start();
        
        auto request = std::make_shared<OCRRequest>(3001, test_image_);
        auto future = request->result_promise.get_future();
        
        no_cls_worker->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "OCR without classification should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        // 打印禁用文本分类的结果
        SimpleTest::printJsonResult(result, "禁用文本方向分类结果");
        
        SimpleTest::assertEquals(3001, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertEquals(3, result["worker_id"].asInt(), "Worker ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "OCR without classification should succeed");
        
        no_cls_worker->stop();
    }
    
    void testPerformanceBenchmark() {
        SimpleTest::printLine("\n=== 性能基准测试 ===");
        
        // 测试优化后的worker性能
        auto worker = std::make_unique<OCRWorker>(4, model_dir_, false, 0, false);
        worker->start();
        
        // 加载测试图像
        cv::Mat test_img = loadTestImageFromFile("card-jd.jpg");
        if (test_img.empty()) {
            test_img = createTestImage();
        }
        
        SimpleTest::printLine("图像尺寸: " + std::to_string(test_img.cols) + "x" + std::to_string(test_img.rows));
        
        // 进行多次测试以获得平均性能
        const int test_count = 3;
        double total_time = 0.0;
        
        for (int i = 0; i < test_count; i++) {
            auto request = std::make_shared<OCRRequest>(4000 + i, test_img);
            auto future = request->result_promise.get_future();
            
            auto start_time = std::chrono::high_resolution_clock::now();
            worker->addRequest(request);
            
            auto status = future.wait_for(std::chrono::seconds(30));
            auto end_time = std::chrono::high_resolution_clock::now();
            
            SimpleTest::assertTrue(status == std::future_status::ready, "Performance test should complete");
            
            std::string result_json = future.get();
            Json::Value result = parseJsonResult(result_json);
            
            double processing_time = result["processing_time_ms"].asDouble();
            auto total_wall_time = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            
            SimpleTest::printLine("第" + std::to_string(i+1) + "次测试:");
            SimpleTest::printLine("  OCR处理时间: " + std::to_string(processing_time) + " ms");
            SimpleTest::printLine("  总耗时(含队列): " + std::to_string(total_wall_time) + " ms");
            
            total_time += processing_time;
            
            // 显示识别结果
            if (result["success"].asBool() && result.isMember("texts")) {
                const Json::Value& texts = result["texts"];
                SimpleTest::printLine("  识别文本数量: " + std::to_string(texts.size()));
                for (size_t j = 0; j < texts.size() && j < 3; j++) {  // 只显示前3个
                    SimpleTest::printLine("    - " + texts[static_cast<int>(j)].asString());
                }
                if (texts.size() > 3) {
                    SimpleTest::printLine("    ... 还有" + std::to_string(texts.size() - 3) + "个结果");
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂休息
        }
        
        double avg_time = total_time / test_count;
        SimpleTest::printLine("\n平均OCR处理时间: " + std::to_string(avg_time) + " ms");
        
        if (avg_time < 300) {
            SimpleTest::printLine("✓ 性能优秀 (< 300ms)");
        } else if (avg_time < 500) {
            SimpleTest::printLine("○ 性能良好 (300-500ms)");
        } else {
            SimpleTest::printLine("△ 性能需要进一步优化 (> 500ms)");
        }
        
        worker->stop();
    }
    
    /**
     * @brief 运行单个测试 - 调试时很有用
     */
    void runSingleTest(const std::string& testName) {
        SimpleTest::printLine("\n=== 运行单个测试: " + testName + " ===");
        
        try {
            setUp();
            
            if (testName == "ConstructorCPU") {
                testConstructorCPU();
            } else if (testName == "StartStop") {
                testStartStop();
            } else if (testName == "MultipleStart") {
                testMultipleStart();
            } else if (testName == "BasicOCRProcessing") {
                testBasicOCRProcessing();
            } else if (testName == "RealImageProcessing") {
                testRealImageProcessing();
            } else if (testName == "EmptyImageProcessing") {
                testEmptyImageProcessing();
            } else if (testName == "ConcurrentProcessing") {
                testConcurrentProcessing();
            } else if (testName == "IdleState") {
                testIdleState();
            } else if (testName == "InvalidModelPath") {
                testInvalidModelPath();
            } else if (testName == "WithTextClassification") {
                testWithTextClassification();
            } else if (testName == "WithoutTextClassification") {
                testWithoutTextClassification();
            } else if (testName == "PerformanceBenchmark") {
                testPerformanceBenchmark();
            } else {
                SimpleTest::printError("未知测试: " + testName);
                SimpleTest::printError("可用测试: ConstructorCPU, StartStop, MultipleStart, BasicOCRProcessing, RealImageProcessing, EmptyImageProcessing, ConcurrentProcessing, IdleState, InvalidModelPath, WithTextClassification, WithoutTextClassification, PerformanceBenchmark");
                return;
            }
            
            tearDown();
            SimpleTest::printLine("=== 测试 " + testName + " 通过 ===");
        }
        catch (const std::exception& e) {
            SimpleTest::printError("=== 测试 " + testName + " 失败: " + std::string(e.what()) + " ===");
            tearDown();
            exit(1);
        }
    }

    void runAllTests() {
        SimpleTest::printLine("开始运行 OCRWorker 测试...");
        
        // 检查必要文件是否存在 - 简单的方式
        std::ifstream models_check("models/det/inference.pdmodel");
        if (!models_check.good()) {
            SimpleTest::printError("警告: 未找到 models 目录或模型文件. 某些测试可能会失败.");
        }
        models_check.close();
        
        try {
            setUp();
            
            testConstructorCPU();
            tearDown();
            
            setUp();
            testStartStop();
            tearDown();
            
            setUp();
            testMultipleStart();
            tearDown();
            
            setUp();
            testBasicOCRProcessing();
            tearDown();
            
            setUp();
            testRealImageProcessing();
            tearDown();
            
            setUp();
            testEmptyImageProcessing();
            tearDown();
            
            setUp();
            testConcurrentProcessing();
            tearDown();
            
            setUp();
            testIdleState();
            tearDown();
            
            setUp();
            testInvalidModelPath();
            tearDown();
            
            setUp();
            testWithTextClassification();
            tearDown();
            
            setUp();
            testWithoutTextClassification();
            tearDown();
            
            SimpleTest::printLine("\n=== 所有测试通过 ===");
        }
        catch (const std::exception& e) {
            SimpleTest::printError("测试失败，异常: " + std::string(e.what()));
            tearDown();
            exit(1);
        }
    }
};

int main(int argc, char* argv[]) {
    // 设置Windows控制台UTF-8支持
    SimpleTest::setupConsole();
    
    OCRWorkerTest test;
    
    // 支持命令行参数来运行特定测试 - 调试时很有用！
    if (argc > 1) {
        std::string testName = argv[1];
        
        // 特殊测试：显示系统信息
        if (testName == "SystemInfo") {
            test.showSystemInfo();
            return 0;
        }
        
        SimpleTest::printLine("运行指定测试: " + testName);
        test.runSingleTest(testName);
    } else {
        SimpleTest::printLine("运行所有测试...");
        SimpleTest::printLine("提示: 使用 'test.exe <TestName>' 运行特定测试进行调试");
        SimpleTest::printLine("可用测试: ConstructorCPU, StartStop, BasicOCRProcessing, WithTextClassification, WithoutTextClassification, PerformanceBenchmark, SystemInfo, 等等");
        test.runAllTests();
    }
    
    return 0;
}
