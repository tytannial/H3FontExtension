cmake_minimum_required(VERSION 3.24)

project(H3CN LANGUAGES CXX RC)

# ============================================================
# 平台约束：游戏为 32 位可执行文件，仅支持 MSVC / Win32
# ============================================================
if(NOT MSVC)
    message(FATAL_ERROR "H3CN 目前仅支持 MSVC 构建")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "目标平台必须是 32 位 (Win32)；请使用 -A Win32 配置")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# 部署选项
# ============================================================

# HoMM3 游戏目录：设置后构建产物自动复制为 BINKW32.DLL / BINKW32.PDB
# （插件通过同名代理 BinkW32 注入游戏进程）
set(H3_PATH "" CACHE PATH "HoMM3 游戏目录（可选，用于构建后自动部署）")

# ============================================================
# Windows XP 兼容（VC-LTL + YY-Thunks）
#
# 默认开启：产出兼容 Windows XP SP3 的二进制（通过 YY-Thunks 提供
# 新 SDK API 的 XP 实现，VC-LTL 链接系统 msvcrt.dll）。
# ============================================================
option(H3CN_TARGET_XP "构建兼容 Windows XP 的二进制（VC-LTL + YY-Thunks）" ON)
set(H3CN_VC_LTL_DIR "" CACHE PATH "本地 VC-LTL 目录（含 _msvcrt.h）；留空自动下载 v5.3.1")
set(H3CN_YY_THUNKS_DIR "" CACHE PATH "本地 YY-Thunks 解压目录（含 build/native/objs）；留空自动下载 1.2.1")

if(H3CN_TARGET_XP)
    include(cmake/WindowsXP.cmake)
endif()

# ============================================================
# 第三方依赖（CMake FetchContent）
#
# 离线/本地开发：
#   -D FETCHCONTENT_SOURCE_DIR_NH3API=<NH3API 源码目录>
#   -D FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=<tomlplusplus 源码目录>
# ============================================================
include(FetchContent)

# NH3API：HoMM III C++ Modding Library（header-only INTERFACE 目标 nh3api）
# 固定 v2.1-91-g55129ba，避免上游 API 变动影响构建
FetchContent_Declare(
    NH3API
    GIT_REPOSITORY https://github.com/void2012/NH3API.git
    GIT_TAG        55129baf5bbcdad63ccd04f639645385610dc489
)

# toml++：TOML 配置文件解析；v3.4.0 与此前内嵌单头版本一致
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(NH3API tomlplusplus)

# ============================================================
# 目标
# ============================================================
add_subdirectory(src)
