#include <iostream>
#include <thread>
#include <sstream>

// 简化版的getWorkerRecommendation方法来演示
std::string getWorkerRecommendation(bool use_gpu, bool enable_cls = false) {
    unsigned int logical_cores = std::thread::hardware_concurrency();
    
    std::ostringstream oss;
    oss << "=== OCR Worker Configuration Recommendation ===\n";
    oss << "System Info:\n";
    oss << "  - Logical CPU Cores (Hardware Threads): " << logical_cores << "\n";
    
    if (use_gpu) {
        oss << "  - Mode: GPU (显存限制)\n";
        oss << "GPU Mode Recommendations:\n";
        if (enable_cls) {
            oss << "  - Memory per Worker: 1250MB GPU (with classifier)\n";
            oss << "  - 4GB GPU: Max 2-3 Workers\n";
            oss << "  - 8GB GPU: Max 5-6 Workers\n";
            oss << "  - 12GB GPU: Max 8-9 Workers\n";
        } else {
            oss << "  - Memory per Worker: 1000MB GPU (no classifier)\n";
            oss << "  - 4GB GPU: Max 3-4 Workers\n";
            oss << "  - 8GB GPU: Max 6-7 Workers\n";
            oss << "  - 12GB GPU: Max 10-11 Workers\n";
        }
    } else {
        oss << "  - Mode: CPU (线程数限制)\n";
        int threads_per_worker = enable_cls ? 6 : 5; // det(2) + rec(2) + cls(1) + main(1)
        
        // 优化Worker数量计算：考虑I/O等待和线程调度效率
        int conservative_workers = std::max(1, static_cast<int>(logical_cores * 0.5 / threads_per_worker));
        int recommended_workers = std::max(1, static_cast<int>(logical_cores * 0.8 / threads_per_worker));
        int aggressive_workers = std::max(2, static_cast<int>(logical_cores * 1.2 / threads_per_worker));
        
        // 对于常见的4核8线程CPU，给出更合理的建议
        if (logical_cores == 8) {
            conservative_workers = enable_cls ? 1 : 1;
            recommended_workers = enable_cls ? 2 : 2;  
            aggressive_workers = enable_cls ? 3 : 3;
        } else if (logical_cores >= 12) {
            // 12线程以上的CPU可以更激进
            conservative_workers = std::max(2, conservative_workers);
            recommended_workers = std::max(3, recommended_workers);
        }
        
        oss << "CPU Mode Recommendations:\n";
        oss << "  - Threads per Worker: " << threads_per_worker << " (det:2, rec:2";
        if (enable_cls) oss << ", cls:1";
        oss << ", main:1)\n";
        oss << "  - Memory per Worker: ~" << (enable_cls ? 170 : 150) << "MB RAM\n";
        oss << "  - Conservative: " << conservative_workers << " Workers (低负载稳定)\n";
        oss << "  - Recommended: " << recommended_workers << " Workers (平衡性能)\n";
        oss << "  - Aggressive: " << aggressive_workers << " Workers (高吞吐量)\n";
        
        // 添加具体的使用建议
        oss << "\n  使用建议:\n";
        oss << "  - 开发测试: " << conservative_workers << " Worker\n";
        oss << "  - 生产环境: " << recommended_workers << " Workers\n";
        oss << "  - 高峰期: " << aggressive_workers << " Workers (需监控CPU使用率)\n";
    }
    
    oss << "\nNote: 以上基于逻辑核心数(" << logical_cores << ")计算，包含超线程/SMT";
    
    return oss.str();
}

int main() {
    std::cout << "=== 4核8线程CPU的Worker配置建议 ===\n\n";
    
    std::cout << "CPU模式 (无分类器):\n";
    std::cout << getWorkerRecommendation(false, false) << std::endl;
    std::cout << "\n" << std::string(60, '=') << "\n\n";
    
    std::cout << "CPU模式 (有分类器):\n";
    std::cout << getWorkerRecommendation(false, true) << std::endl;
    std::cout << "\n" << std::string(60, '=') << "\n\n";
    
    std::cout << "GPU模式 (无分类器):\n";
    std::cout << getWorkerRecommendation(true, false) << std::endl;
    std::cout << "\n" << std::string(60, '=') << "\n\n";
    
    std::cout << "GPU模式 (有分类器):\n";
    std::cout << getWorkerRecommendation(true, true) << std::endl;
    
    return 0;
}
