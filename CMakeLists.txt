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

# TODO(后续): YY-Thunks + VC-LTL5 集成（Windows XP 兼容），
# 通过 option(H3CN_TARGET_XP OFF) 开关引入，本次暂不启用。
