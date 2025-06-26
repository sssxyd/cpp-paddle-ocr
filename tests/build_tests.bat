@echo off
setlocal enabledelayedexpansion

echo Building OCRWorker Simple Tests...

REM 创建构建目录
if not exist "build" mkdir build
cd build

REM 设置编译器选项
set COMPILER_FLAGS=/std:c++20 /utf-8 /EHsc /TP /MT /W3 /O2 /Zi /FS /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DBASE64_STATIC_DEFINE

REM 设置包含目录
set INCLUDE_DIRS=/I "%~dp0..\include" /I "%MSVC_INCLUDE%" /I "%WIN_SDK_INCLUDE%\ucrt" /I "%WIN_SDK_INCLUDE%\shared" /I "%WIN_SDK_INCLUDE%\um" /I "%VCPKG_STATIC%\include" /I "%VCPKG_STATIC%\include\opencv4"

REM 设置源文件
set TEST_SOURCES="%~dp0test_ocr_worker.cpp"
set PROJECT_SOURCES="%~dp0..\src\ocr_worker.cpp" "%~dp0..\src\ocr_det.cpp" "%~dp0..\src\ocr_rec.cpp" "%~dp0..\src\ocr_cls.cpp" "%~dp0..\src\clipper.cpp" "%~dp0..\src\postprocess_op.cpp" "%~dp0..\src\preprocess_op.cpp" "%~dp0..\src\utility.cpp"

REM 设置库路径
set LIB_PATHS=/LIBPATH:"%MSVC_LIB%" /LIBPATH:"%WIN_SDK_LIB%\ucrt\x64" /LIBPATH:"%WIN_SDK_LIB%\um\x64" /LIBPATH:"%VCPKG_STATIC%\lib" /LIBPATH:"%~dp0..\lib\msvc"

REM 设置链接库
set LIBRARIES=paddle_inference.lib opencv_highgui4.lib opencv_imgcodecs4.lib opencv_imgproc4.lib opencv_core4.lib gflags_static.lib jsoncpp.lib base64.lib libwebp.lib libwebpdemux.lib libwebpmux.lib libwebpdecoder.lib libsharpyuv.lib zlib.lib libpng16.lib tiff.lib lzma.lib turbojpeg.lib kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib shlwapi.lib

REM 编译
echo Compiling simple test executable...
cl.exe %COMPILER_FLAGS% %TEST_SOURCES% %PROJECT_SOURCES% %INCLUDE_DIRS% /Fe:test_ocr_worker.exe /Fo:.\ /Fd:test_ocr_worker.pdb /link /DEBUG /PDB:test_ocr_worker.pdb %LIB_PATHS% %LIBRARIES%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

REM 复制依赖文件
echo Copying dependencies...
if exist "%~dp0..\bin\msvc\*.dll" (
    copy "%~dp0..\bin\msvc\*.dll" . >nul
)

if exist "C:\Windows\System32\vcomp140.dll" (
    copy "C:\Windows\System32\vcomp140.dll" . >nul
)

if exist "%~dp0..\models" (
    if not exist "models" mkdir models
    xcopy "%~dp0..\models\*" "models\" /E /I /Y >nul
)

if exist "%~dp0..\images" (
    if not exist "images" mkdir images
    xcopy "%~dp0..\images\*" "images\" /E /I /Y >nul
)

echo Build completed successfully!
echo.
echo To run tests, execute: test_ocr_worker.exe
cd ..
