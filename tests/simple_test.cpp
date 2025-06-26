#include "simple_test.h"
#include <exception>

void SimpleTest::assertEquals(int expected, int actual, const std::string& message) {
    if (expected != actual) {
        std::cerr << "FAILED: " << message << " - Expected: " << expected << ", Actual: " << actual << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void SimpleTest::assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void SimpleTest::assertFalse(bool condition, const std::string& message) {
    if (condition) {
        std::cerr << "FAILED: " << message << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void SimpleTest::assertNotNull(void* ptr, const std::string& message) {
    if (ptr == nullptr) {
        std::cerr << "FAILED: " << message << " - Pointer is null" << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void SimpleTest::expectNoThrow(std::function<void()> func, const std::string& message) {
    try {
        func();
        std::cout << "PASSED: " << message << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << message << " - Exception: " << e.what() << std::endl;
        exit(1);
    }
}

void SimpleTest::expectThrow(std::function<void()> func, const std::string& message) {
    try {
        func();
        std::cerr << "FAILED: " << message << " - Expected exception but none was thrown" << std::endl;
        exit(1);
    } catch (const std::exception& e) {
        std::cout << "PASSED: " << message << " - Exception caught: " << e.what() << std::endl;
    }
}
