# ============================================================
# Windows XP 兼容集成（VC-LTL + YY-Thunks）
#
# - VC-LTL：使用系统 msvcrt.dll 的 CRT 实现（替代 VC 运行库，
#   解决旧系统无运行库与 FLS 限制问题）
# - YY-Thunks：为新 Windows SDK 引入的 API 提供 XP 实现（obj 链接）
#
# 依赖获取：
#   优先使用本地目录（H3CN_VC_LTL_DIR / H3CN_YY_THUNKS_DIR），
#   否则自动从官方 Release / NuGet 下载（版本与 SHA256 固定）。
# ============================================================

include_guard(GLOBAL)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Windows XP 兼容构建仅支持 32 位 MSVC（-A Win32）")
endif()

# 依赖下载缓存目录（build/ 下的共享缓存，避免各预设重复下载）
set(_h3cn_xp_deps_dir "${CMAKE_SOURCE_DIR}/build/_h3cn_deps")
file(MAKE_DIRECTORY "${_h3cn_xp_deps_dir}")

# ------------------------------------------------------------
# VC-LTL（v5.3.1 Binary）
# ------------------------------------------------------------
set(_vc_ltl_root "${H3CN_VC_LTL_DIR}")
if(NOT _vc_ltl_root)
    set(_vc_ltl_root "${_h3cn_xp_deps_dir}/vc-ltl")
    if(NOT EXISTS "${_vc_ltl_root}/_msvcrt.h")
        set(_vc_ltl_archive "${_h3cn_xp_deps_dir}/VC-LTL-Binary.7z")
        message(STATUS "下载 VC-LTL v5.3.1（约 18 MB）...")
        file(DOWNLOAD
            "https://github.com/Chuyu-Team/VC-LTL5/releases/download/v5.3.1/VC-LTL-Binary.7z"
            "${_vc_ltl_archive}"
            EXPECTED_HASH SHA256=7A18799ED3AA84A225610A5447A56BC534C5C98CCB8DEC05CABA0E3F633431AD
            TIMEOUT 900
            INACTIVITY_TIMEOUT 60
            STATUS _vc_ltl_download_status)
        list(GET _vc_ltl_download_status 0 _vc_ltl_download_code)
        if(NOT _vc_ltl_download_code EQUAL 0)
            message(FATAL_ERROR
                "VC-LTL 下载失败：${_vc_ltl_download_status}\n"
                "请手动下载 https://github.com/Chuyu-Team/VC-LTL5/releases/download/v5.3.1/VC-LTL-Binary.7z\n"
                "解压后通过 -DH3CN_VC_LTL_DIR=<目录> 指定，或检查网络。")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_vc_ltl_archive}" DESTINATION "${_vc_ltl_root}")
    endif()
endif()

if(NOT EXISTS "${_vc_ltl_root}/_msvcrt.h")
    message(FATAL_ERROR "未找到 VC-LTL（${_vc_ltl_root} 缺少 _msvcrt.h）：请设置 H3CN_VC_LTL_DIR")
endif()

# ------------------------------------------------------------
# YY-Thunks（v1.2.1 NuGet 包，zip 结构）
# ------------------------------------------------------------
set(_yy_thunks_root "${H3CN_YY_THUNKS_DIR}")
if(NOT _yy_thunks_root)
    set(_yy_thunks_root "${_h3cn_xp_deps_dir}/yy-thunks")
    if(NOT EXISTS "${_yy_thunks_root}/build/native/objs/x86/YY_Thunks_for_WinXP.obj")
        set(_yy_thunks_archive "${_h3cn_xp_deps_dir}/yy-thunks.1.2.1.nupkg")
        message(STATUS "下载 YY-Thunks 1.2.1（约 7 MB）...")
        file(DOWNLOAD
            "https://api.nuget.org/v3-flatcontainer/yy-thunks/1.2.1/yy-thunks.1.2.1.nupkg"
            "${_yy_thunks_archive}"
            EXPECTED_HASH SHA256=D2C91D654EFFE564A4D6F976DA6E501F672692E74468ECBCA774E47E27D2B468
            TIMEOUT 600
            INACTIVITY_TIMEOUT 60
            STATUS _yy_thunks_download_status)
        list(GET _yy_thunks_download_status 0 _yy_thunks_download_code)
        if(NOT _yy_thunks_download_code EQUAL 0)
            message(FATAL_ERROR
                "YY-Thunks 下载失败：${_yy_thunks_download_status}\n"
                "请手动下载 https://www.nuget.org/packages/YY-Thunks/1.2.1\n"
                "解压后通过 -DH3CN_YY_THUNKS_DIR=<目录> 指定，或检查网络。")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_yy_thunks_archive}" DESTINATION "${_yy_thunks_root}")
    endif()
endif()

set(H3CN_YY_THUNKS_OBJ "${_yy_thunks_root}/build/native/objs/x86/YY_Thunks_for_WinXP.obj")
if(NOT EXISTS "${H3CN_YY_THUNKS_OBJ}")
    message(FATAL_ERROR "未找到 YY-Thunks 目标文件（${H3CN_YY_THUNKS_OBJ}）：请设置 H3CN_YY_THUNKS_DIR")
endif()

# ------------------------------------------------------------
# 注入 VC-LTL 配置（创建 VC_LTL INTERFACE 目标，供目标链接）
# ------------------------------------------------------------
# 显式指定平台（config.cmake 官方支持的"外部定义"方式）：
# 项目固定为 32 位 Win32，避免内置平台探测对 CMake 版本的兼容问题
set(LTLPlatform "Win32")
set(WindowsTargetPlatformMinVersion "5.1.2600.0") # 最低 Windows XP
set(SupportLTL "msvcrt")                          # 使用系统 msvcrt.dll
set(VC_LTL_Root "${_vc_ltl_root}")
set(VC_LTL_EnableCMakeInterface ON)
include("${VC_LTL_Root}/config/config.cmake")

message(STATUS "Windows XP 兼容已启用：VC-LTL（${_vc_ltl_root}）+ YY-Thunks（${_yy_thunks_root}）")
