# ============================================================
# 纯逻辑测试（不依赖游戏进程）：GBK 编码判定/转换、颜色转换
#
# 复用插件源码中的编码与工具实现；ExtFont::Load 引用字形后端工厂，
# 因此一并编译 GlyphRasterizer 与 Log（均为纯 Win32，无游戏依赖）。
# ============================================================

add_executable(H3CNTests
    H3CNTests.cpp
    "${CMAKE_SOURCE_DIR}/src/ExtFont.cpp"
    "${CMAKE_SOURCE_DIR}/src/GlyphRasterizer.cpp"
    "${CMAKE_SOURCE_DIR}/src/GlyphRasterizerDWrite.cpp"
    "${CMAKE_SOURCE_DIR}/src/GlyphRasterizerGdi.cpp"
    "${CMAKE_SOURCE_DIR}/src/Log.cpp"
)

target_include_directories(H3CNTests PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_compile_definitions(H3CNTests PRIVATE
    WIN32
    _X86_
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_options(H3CNTests PRIVATE /utf-8 /W4 /sdl /external:anglebrackets /external:W0)

target_link_libraries(H3CNTests PRIVATE nh3api)

add_test(NAME H3CNTests COMMAND H3CNTests)
