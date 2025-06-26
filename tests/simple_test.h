#pragma once

#include <iostream>
#include <functional>
#include <string>
#include <json/json.h>

/**
 * @brief 简单的测试框架
 */
class SimpleTest {
public:
    static void assertEquals(int expected, int actual, const std::string& message);
    static void assertTrue(bool condition, const std::string& message);
    static void assertFalse(bool condition, const std::string& message);
    static void assertNotNull(void* ptr, const std::string& message);
    static void expectNoThrow(std::function<void()> func, const std::string& message);
    static void expectThrow(std::function<void()> func, const std::string& message);
    
    // Windows 控制台UTF-8支持
    static void setupConsole();
    static std::wstring utf8ToWideString(const std::string& utf8_str);
    
    // UTF-8 安全输出方法
    static void printLine(const std::string& utf8_message);
    static void printError(const std::string& utf8_message);
    
    // JSON 美化打印方法
    static void printJsonResult(const Json::Value& json_value, const std::string& title = "JSON结果");
};
