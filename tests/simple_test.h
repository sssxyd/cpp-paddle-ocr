#pragma once

#include <iostream>
#include <functional>
#include <string>

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
};
