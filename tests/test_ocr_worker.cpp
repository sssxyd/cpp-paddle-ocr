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
        std::cout << "\n=== Testing OCRWorker Constructor (CPU) ===" << std::endl;
        
        SimpleTest::expectNoThrow([this]() {
            worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        }, "OCRWorker constructor should not throw");
        
        SimpleTest::assertNotNull(worker_.get(), "Worker should not be null");
        SimpleTest::assertEquals(1, worker_->getWorkerId(), "Worker ID should be 1");
        SimpleTest::assertTrue(worker_->isIdle(), "Worker should be idle initially");
    }
    
    void testStartStop() {
        std::cout << "\n=== Testing OCRWorker Start/Stop ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        
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
        std::cout << "\n=== Testing Multiple Start Calls ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        
        SimpleTest::expectNoThrow([this]() {
            worker_->start();
            worker_->start();
        }, "Multiple start calls should not throw");
        
        worker_->stop();
    }
    
    void testBasicOCRProcessing() {
        std::cout << "\n=== Testing Basic OCR Processing ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        worker_->start();
        
        auto request = std::make_shared<OCRRequest>(1001, test_image_);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "OCR processing should complete within 30 seconds");
        
        std::string result_json = future.get();
        SimpleTest::assertTrue(!result_json.empty(), "Result JSON should not be empty");
        
        Json::Value result = parseJsonResult(result_json);
        SimpleTest::assertEquals(1001, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertEquals(1, result["worker_id"].asInt(), "Worker ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "OCR should succeed");
        SimpleTest::assertTrue(result["processing_time_ms"].asDouble() > 0.0, "Processing time should be positive");
        
        worker_->stop();
    }
    
    void testRealImageProcessing() {
        std::cout << "\n=== Testing Real Image Processing ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        worker_->start();
        
        cv::Mat real_image = loadTestImageFromFile("title.jpg");
        
        auto request = std::make_shared<OCRRequest>(1002, real_image);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(30));
        SimpleTest::assertTrue(status == std::future_status::ready, "Real image processing should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        SimpleTest::assertEquals(1002, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertTrue(result["success"].asBool(), "Real image OCR should succeed");
        
        if (result["success"].asBool()) {
            SimpleTest::assertTrue(result.isMember("texts"), "Result should contain texts");
            SimpleTest::assertTrue(result.isMember("boxes"), "Result should contain boxes");
            
            std::cout << "OCR Results:" << std::endl;
            const Json::Value& texts = result["texts"];
            for (const auto& text : texts) {
                std::cout << "  - " << text.asString() << std::endl;
            }
        }
        
        worker_->stop();
    }
    
    void testEmptyImageProcessing() {
        std::cout << "\n=== Testing Empty Image Processing ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        worker_->start();
        
        auto request = std::make_shared<OCRRequest>(1003, empty_image_);
        auto future = request->result_promise.get_future();
        
        worker_->addRequest(request);
        
        auto status = future.wait_for(std::chrono::seconds(10));
        SimpleTest::assertTrue(status == std::future_status::ready, "Empty image processing should complete");
        
        std::string result_json = future.get();
        Json::Value result = parseJsonResult(result_json);
        
        SimpleTest::assertEquals(1003, result["request_id"].asInt(), "Request ID should match");
        SimpleTest::assertFalse(result["success"].asBool(), "Empty image should fail");
        SimpleTest::assertTrue(result.isMember("error"), "Result should contain error message");
        
        worker_->stop();
    }
    
    void testConcurrentProcessing() {
        std::cout << "\n=== Testing Concurrent Processing ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
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
        std::cout << "\n=== Testing Idle State ===" << std::endl;
        
        worker_ = std::make_unique<OCRWorker>(1, model_dir_, false, 0);
        
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
        std::cout << "\n=== Testing Invalid Model Path ===" << std::endl;
        
        SimpleTest::expectThrow([this]() {
            auto invalid_worker = std::make_unique<OCRWorker>(1, "invalid_model_path", false, 0);
        }, "Invalid model path should throw exception");
    }
    
    /**
     * @brief 运行单个测试 - 调试时很有用
     */
    void runSingleTest(const std::string& testName) {
        std::cout << "\n=== Running Single Test: " << testName << " ===" << std::endl;
        
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
            } else {
                std::cerr << "Unknown test: " << testName << std::endl;
                std::cerr << "Available tests: ConstructorCPU, StartStop, MultipleStart, BasicOCRProcessing, RealImageProcessing, EmptyImageProcessing, ConcurrentProcessing, IdleState, InvalidModelPath" << std::endl;
                return;
            }
            
            tearDown();
            std::cout << "=== Test " << testName << " PASSED ===" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "=== Test " << testName << " FAILED: " << e.what() << " ===" << std::endl;
            tearDown();
            exit(1);
        }
    }

    void runAllTests() {
        std::cout << "Starting OCRWorker tests..." << std::endl;
        
        // 检查必要文件是否存在 - 简单的方式
        std::ifstream models_check("models/det/inference.pdmodel");
        if (!models_check.good()) {
            std::cerr << "Warning: models directory or model files not found. Some tests may fail." << std::endl;
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
            
            std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Test failed with exception: " << e.what() << std::endl;
            tearDown();
            exit(1);
        }
    }
};

int main(int argc, char* argv[]) {
    OCRWorkerTest test;
    
    // 支持命令行参数来运行特定测试 - 调试时很有用！
    if (argc > 1) {
        std::string testName = argv[1];
        std::cout << "Running specific test: " << testName << std::endl;
        test.runSingleTest(testName);
    } else {
        std::cout << "Running all tests..." << std::endl;
        std::cout << "Tip: Use 'test.exe <TestName>' to run a specific test for debugging" << std::endl;
        std::cout << "Available tests: ConstructorCPU, StartStop, BasicOCRProcessing, etc." << std::endl;
        test.runAllTests();
    }
    
    return 0;
}
