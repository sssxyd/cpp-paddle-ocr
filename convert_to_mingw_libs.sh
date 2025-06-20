#!/bin/bash
# filepath: convert_to_mingw_libs.sh

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 全局计数器
PROCESSED_COUNT=0
SKIPPED_COUNT=0
ERROR_COUNT=0

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查必要工具
check_tools() {
    print_info "检查必要工具..."
    
    if ! command -v gendef >/dev/null 2>&1; then
        print_error "gendef 工具未找到，请安装: pacman -S mingw-w64-ucrt-x86_64-tools-git"
        exit 1
    fi
    
    if ! command -v dlltool >/dev/null 2>&1; then
        print_error "dlltool 工具未找到，请安装: pacman -S mingw-w64-ucrt-x86_64-binutils"
        exit 1
    fi
    
    if ! command -v ar >/dev/null 2>&1; then
        print_error "ar 工具未找到，请安装: pacman -S mingw-w64-ucrt-x86_64-binutils"
        exit 1
    fi
    
    print_success "工具检查完成"
}

# 处理动态链接库 (DLL + LIB -> DLL.A)
process_dynamic_lib() {
    local dll_file="$1"
    local lib_file="$2"
    local base_name=$(basename "$dll_file" .dll)
    local def_file="${base_name}.def"
    
    # 检查base_name是否以lib开头，不是才加lib前缀
    local output_file
    if [[ "$base_name" == lib* ]]; then
        output_file="${base_name}.dll.a"
    else
        output_file="lib${base_name}.dll.a"
    fi
        
    # 检查输入文件是否存在
    if [ ! -f "$dll_file" ]; then
        print_error "DLL文件不存在: $dll_file"
        ((ERROR_COUNT++))
        return 1
    fi
    
    # 检查输出文件是否已存在
    if [ -f "$output_file" ]; then
        print_warning "跳过 $dll_file: $output_file 已存在"
        ((SKIPPED_COUNT++))
        return 0
    fi
    
    print_info "处理动态库: $dll_file"
    
    # 清理之前可能残留的临时文件
    rm -f "$def_file"
    
    # 生成.def文件
    if gendef "$dll_file" 2>/dev/null; then
        # 检查生成的DEF文件是否有效
        if [ ! -s "$def_file" ]; then
            print_error "生成的DEF文件为空: $def_file"
            rm -f "$def_file"
            ((ERROR_COUNT++))
            return 1
        fi
        
        # 生成.dll.a导入库
        if dlltool -d "$def_file" -l "$output_file" -D "$dll_file" 2>/dev/null; then
            # 验证生成的导入库
            if [ -f "$output_file" ] && [ -s "$output_file" ]; then
                print_success "生成: $output_file"
                ((PROCESSED_COUNT++))
            else
                print_error "生成的导入库无效: $output_file"
                rm -f "$output_file"
                ((ERROR_COUNT++))
            fi
            
            # 清理临时文件
            rm -f "$def_file"
        else
            print_error "无法生成 $output_file (dlltool失败)"
            ((ERROR_COUNT++))
            rm -f "$def_file"
        fi
    else
        print_error "无法为 $dll_file 生成DEF文件 (gendef失败)"
        ((ERROR_COUNT++))
    fi
}

# 处理单个lib目录
process_lib_directory() {
    local lib_dir="$1"
    
    print_info "处理目录: $lib_dir"
    
    # 进入lib目录
    pushd "$lib_dir" >/dev/null

    # 先删除所有现有的 .a 文件
    if ls *.a >/dev/null 2>&1; then
        print_info "删除现有的 .a 文件..."
        local a_files=($(ls *.a 2>/dev/null))
        for a_file in "${a_files[@]}"; do
            if rm -f "$a_file" 2>/dev/null; then
                print_info "已删除: $a_file"
            else
                print_warning "无法删除: $a_file"
            fi
        done
    fi    
    
    # 获取所有.dll和.lib文件
    local dll_files=($(ls *.dll 2>/dev/null || true))
    local lib_files=($(ls *.lib 2>/dev/null || true))
    
    if [ ${#dll_files[@]} -eq 0 ] && [ ${#lib_files[@]} -eq 0 ]; then
        print_warning "目录 $lib_dir 中没有找到DLL或LIB文件"
        popd >/dev/null
        return
    fi
    
    # 创建关联数组存储基础名称
    declare -A dll_bases lib_bases
    
    # 收集DLL基础名称
    for dll in "${dll_files[@]}"; do
        base=$(basename "$dll" .dll)
        dll_bases["$base"]="$dll"
    done
    
    # 收集LIB基础名称
    for lib in "${lib_files[@]}"; do
        base=$(basename "$lib" .lib)
        lib_bases["$base"]="$lib"
    done
    
    # 处理动态链接库 (同时存在.dll和.lib)
    for base in "${!dll_bases[@]}"; do
        if [[ -n "${lib_bases[$base]:-}" ]]; then
            process_dynamic_lib "${dll_bases[$base]}" "${lib_bases[$base]}"
            # 从lib_bases中移除已处理的项
            unset lib_bases["$base"]
        fi
    done
    
    popd >/dev/null
}

# 主函数
main() {
    local msvc_dir="${1:-paddle_inference}"
    
    print_info "开始转换Paddle Inference库文件"
    print_info "目标目录: $msvc_dir"
    
    # 检查paddle_inference目录是否存在
    if [ ! -d "$msvc_dir" ]; then
        print_error "目录 $msvc_dir 不存在"
        exit 1
    fi
    
    # 检查必要工具
    check_tools
    
    # 查找所有名为lib的目录
    local lib_dirs=($(find "$msvc_dir" -type d -name "lib"))
    
    if [ ${#lib_dirs[@]} -eq 0 ]; then
        print_warning "在 $msvc_dir 中没有找到名为 'lib' 的目录"
        exit 0
    fi
    
    print_info "找到 ${#lib_dirs[@]} 个lib目录"
    
    # 处理每个lib目录
    for lib_dir in "${lib_dirs[@]}"; do
        process_lib_directory "$lib_dir"
        echo # 添加空行分隔
    done
    
    # 输出统计信息
    echo "================================="
    print_success "转换完成!"
    print_info "处理成功: $PROCESSED_COUNT 个文件"
    print_warning "跳过: $SKIPPED_COUNT 个文件"
    print_error "错误: $ERROR_COUNT 个文件"
    echo "================================="
}

# 运行主函数
main "$@"