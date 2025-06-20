cmake_minimum_required(VERSION 3.20)
project(rpa-windows-ocr)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译器选项
if(MSVC)
    # 对应 tasks.json 中的编译器选项
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /EHsc /W3")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Zi /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Od /Zi /D_DEBUG")
    
    # 预处理器定义
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
    
    # 链接器选项
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:msvcprtd.lib")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:msvcrtd.lib")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:libcmtd.lib")
endif()

# 设置默认构建类型为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 查找 vcpkg 包
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
find_package(glog REQUIRED)
find_package(gflags REQUIRED)

# 或者手动设置路径（如果 find_package 不工作）
if(NOT OpenCV_FOUND)
    set(VCPKG_ROOT $ENV{VCPKG_ROOT})
    if(NOT VCPKG_ROOT)
        set(VCPKG_ROOT "E:/vcpkg")
    endif()
    
    set(VCPKG_INSTALLED "${VCPKG_ROOT}/installed/x64-windows-static")
    
    # 包含目录
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${VCPKG_INSTALLED}/include
        ${VCPKG_INSTALLED}/include/opencv4
    )
    
    # 库目录
    link_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/msvc
        ${VCPKG_INSTALLED}/lib
    )
    
    # OpenCV 库
    set(OpenCV_LIBS
        opencv_highgui4
        opencv_imgcodecs4
        opencv_imgproc4
        opencv_core4
    )
    
    # 其他第三方库
    set(THIRD_PARTY_LIBS
        glog
        gflags_static
        libwebp
        libwebpdemux
        libwebpmux
        libwebpdecoder
        libsharpyuv
        zlib
        libpng16
        tiff
        lzma
        turbojpeg
    )
    
    # Windows 系统库
    set(SYSTEM_LIBS
        kernel32
        user32
        gdi32
        advapi32
        shell32
        ole32
        oleaut32
        uuid
        comdlg32
    )
endif()

# 源文件
set(SOURCES
    test_paddle_ocr.cpp
    paddle_ocr.cpp
)

# 创建可执行文件
add_executable(test_paddle_ocr ${SOURCES})

# 链接库
if(OpenCV_FOUND)
    target_link_libraries(test_paddle_ocr 
        ${OpenCV_LIBS}
        glog::glog
        gflags::gflags
        paddle_inference
    )
else()
    target_link_libraries(test_paddle_ocr
        paddle_inference
        ${OpenCV_LIBS}
        ${THIRD_PARTY_LIBS}
        ${SYSTEM_LIBS}
    )
endif()

# 设置输出目录
set_target_properties(test_paddle_ocr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)