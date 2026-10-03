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

set_target_properties(H3CN PROPERTIES
    OUTPUT_NAME H3CN
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
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
        COMMENT "部署 H3CN 到 ${H3_PATH}"
    )
endif()
