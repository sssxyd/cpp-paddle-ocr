#include "simple_test.h"
#include <exception>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <locale>
#endif

void SimpleTest::assertEquals(int expected, int actual, const std::string& message) {
    if (expected != actual) {
        printError("FAILED: " + message + " - Expected: " + std::to_string(expected) + ", Actual: " + std::to_string(actual));
        exit(1);
    }
    printLine("PASSED: " + message);
}

void SimpleTest::assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        printError("FAILED: " + message);
        exit(1);
    }
    printLine("PASSED: " + message);
}

void SimpleTest::assertFalse(bool condition, const std::string& message) {
    if (condition) {
        printError("FAILED: " + message);
        exit(1);
    }
    printLine("PASSED: " + message);
}

void SimpleTest::assertNotNull(void* ptr, const std::string& message) {
    if (ptr == nullptr) {
        printError("FAILED: " + message + " - Pointer is null");
        exit(1);
    }
    printLine("PASSED: " + message);
}

void SimpleTest::expectNoThrow(std::function<void()> func, const std::string& message) {
    try {
        func();
        printLine("PASSED: " + message);
    } catch (const std::exception& e) {
        printError("FAILED: " + message + " - Exception: " + std::string(e.what()));
        exit(1);
    }
}

void SimpleTest::expectThrow(std::function<void()> func, const std::string& message) {
    try {
        func();
        printError("FAILED: " + message + " - Expected exception but none was thrown");
        exit(1);
    } catch (const std::exception& e) {
        printLine("PASSED: " + message + " - Exception caught: " + std::string(e.what()));
    }
}

#ifdef _WIN32
std::wstring SimpleTest::utf8ToWideString(const std::string& utf8_str) {
    if (utf8_str.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8_str[0], (int)utf8_str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void SimpleTest::setupConsole() {
    // 设置控制台代码页为UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // 设置控制台模式以支持Unicode
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    // 设置locale为UTF-8
    std::locale::global(std::locale(""));
}

void SimpleTest::printLine(const std::string& utf8_message) {
    std::wcout << utf8ToWideString(utf8_message) << std::endl;
}

void SimpleTest::printError(const std::string& utf8_message) {
    std::wcerr << utf8ToWideString(utf8_message) << std::endl;
}

void SimpleTest::printJsonResult(const Json::Value& json_value, const std::string& title) {
    printLine("\n--- " + title + " ---");
    
    // 使用Json::StreamWriterBuilder来格式化输出
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  "; // 使用2个空格缩进
    builder["precision"] = 6;
    builder["precisionType"] = "significant";
    
    std::ostringstream oss;
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(json_value, &oss);
    
    printLine(oss.str());
    printLine("--- " + title + " 结束 ---\n");
}
#else
// 非Windows平台的空实现
std::wstring SimpleTest::utf8ToWideString(const std::string& utf8_str) {
    // 在非Windows平台上，可以直接返回空或进行其他处理
    return std::wstring();
}

void SimpleTest::setupConsole() {
    // 在非Windows平台上不需要特殊设置
}

void SimpleTest::printLine(const std::string& utf8_message) {
    std::cout << utf8_message << std::endl;
}

void SimpleTest::printError(const std::string& utf8_message) {
    std::cerr << utf8_message << std::endl;
}

void SimpleTest::printJsonResult(const Json::Value& json_value, const std::string& title) {
    printLine("\n--- " + title + " ---");
    
    // 使用Json::StreamWriterBuilder来格式化输出
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  "; // 使用2个空格缩进
    builder["precision"] = 6;
    builder["precisionType"] = "significant";
    
    std::ostringstream oss;
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(json_value, &oss);
    
    printLine(oss.str());
    printLine("--- " + title + " 结束 ---\n");
}
#endif
