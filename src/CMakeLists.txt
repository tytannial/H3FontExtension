# ============================================================
# H3CN 插件（代理 BinkW32.DLL 注入游戏）
# ============================================================

add_library(H3CN SHARED
    dllmain.cpp
    ExtFont.cpp
    ExtFont.h
    GlyphRasterizer.cpp
    GlyphRasterizer.h
    GlyphRasterizerDWrite.cpp
    GlyphRasterizerGdi.cpp
    H3FontExtension.cpp
    H3FontExtension.h
    H3FontExtensionInternal.h
    Log.cpp
    Log.h
    TextLayout.cpp
    TextRender.cpp
    hook/binkw32hack.h
    resource.h
    H3CN.rc
    H3CN.toml
)

target_compile_definitions(H3CN PRIVATE
    WIN32
    _X86_
    _WINDOWS
    _USRDLL
    H3CN_EXPORTS
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    UNICODE
    _UNICODE
)

# /W4 全警告等级；尖括号包含的第三方/系统头（NH3API、Windows SDK）豁免警告
target_compile_options(H3CN PRIVATE /utf-8 /W4 /sdl /external:anglebrackets /external:W0)

target_link_libraries(H3CN PRIVATE
    nh3api
    tomlplusplus::tomlplusplus
)

# Windows XP 兼容：VC-LTL（系统 msvcrt.dll CRT）+ YY-Thunks（新 API 的 XP 实现）
# 说明：不降低 WINVER/_WIN32_WINNT——YY-Thunks 的用途即为"新 SDK 编译、旧系统运行"，
# 降低 SDK 版本宏反而会裁掉 DirectWrite 等头文件声明
if(H3CN_TARGET_XP)
    target_link_libraries(H3CN PRIVATE VC_LTL "${H3CN_YY_THUNKS_OBJ}")
    target_compile_options(H3CN PRIVATE /Zc:threadSafeInit-) # 避免静态局部初始化依赖 Vista+ API
    target_link_options(H3CN PRIVATE
        "/ENTRY:DllMainCRTStartupForYY_Thunks" # YY-Thunks 的 DLL 启动包装（thread_local 析构支持）
        "/SUBSYSTEM:WINDOWS,5.01"              # 最低系统版本：Windows XP x86
    )
endif()

# 运行库：静态 CRT（/MT、/MTd）。
# XP 模式下 VC-LTL 会将静态 CRT 的实现替换为基于系统 msvcrt.dll 的版本，
# STL 保持静态链接（不引入 MSVCP140.dll 依赖，XP 上可用）
set(H3CN_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_target_properties(H3CN PROPERTIES
    OUTPUT_NAME H3CN
    MSVC_RUNTIME_LIBRARY "${H3CN_RUNTIME_LIBRARY}"
)

# 全程序优化（对应原 vcxproj 的 Release WholeProgramOptimization）
# Release 保留调试信息与 PDB（对应 GenerateDebugInformation=true，供部署到游戏目录调试）
set_target_properties(H3CN PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
target_compile_options(H3CN PRIVATE "$<$<CONFIG:Release>:/Zi>")
target_link_options(H3CN PRIVATE "$<$<CONFIG:Release>:/DEBUG>")

# 调试可视化：NH3API 容器的 natvis（可选，随源码提供）
if(EXISTS "${nh3api_SOURCE_DIR}/debugging/nh3api_std.natvis")
    target_sources(H3CN PRIVATE "${nh3api_SOURCE_DIR}/debugging/nh3api_std.natvis")
endif()

# 配置文件复制到输出目录（对应原 vcxproj CopyFileToFolders）
add_custom_command(TARGET H3CN POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/H3CN.toml"
            "$<TARGET_FILE_DIR:H3CN>/H3CN.toml"
    COMMENT "复制 H3CN.toml 到输出目录"
)

# 部署到游戏目录：H3CN.dll/PDB 复制为 BINKW32.DLL/PDB（同名代理注入）
if(H3_PATH)
    add_custom_command(TARGET H3CN POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:H3CN>" "${H3_PATH}/BINKW32.DLL"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_PDB_FILE:H3CN>" "${H3_PATH}/BINKW32.PDB"
        # 默认配置仅在目标不存在时复制，避免覆盖用户已调整的字体配置
        COMMAND "${CMAKE_COMMAND}"
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/H3CN.toml"
                "-DDST=${H3_PATH}/H3CN.toml"
                -P "${CMAKE_SOURCE_DIR}/cmake/CopyIfNotExists.cmake"
        COMMENT "部署 H3CN 到 ${H3_PATH}"
    )
endif()
