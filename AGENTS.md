# AGENTS.md

HoMM3（英雄无敌3）简体中文扩展插件：32 位 DLL，以同名代理替换 `binkw32.dll` 注入游戏进程，Hook 游戏原版字体绘制/测量/拆行函数。无 CI、无 lint 配置；`tests/` 提供不依赖游戏的纯逻辑测试。

## 构建

```bat
cmake --preset vs2026-x86          && cmake --build --preset release
cmake --preset vs2026-x86-local    && cmake --build --preset release-local
```

- `CMakePresets.json` 只提供 vs2026-x86 系列（VS 18 2026 生成器，Win32）；无 VS2022 preset
- `*-local` 复用硬编码路径 `D:/CodeProject/public/NH3API`，离线可用
- `-DH3_PATH=<游戏目录>` 使构建后自动把产物复制为 `BINKW32.DLL`/`BINKW32.PDB`
- 首次配置需联网下载 XP 依赖（VC-LTL/YY-Thunks，固定 SHA256）；离线传 `-DH3CN_VC_LTL_DIR` / `-DH3CN_YY_THUNKS_DIR`；`-DH3CN_TARGET_XP=OFF` 可关闭 XP 兼容
- 测试（纯逻辑：GBK 判定/转换、颜色转换）：`ctest --test-dir build/vs2026-x86 -C Release --output-on-failure`；`-DH3CN_BUILD_TESTS=OFF` 可关闭
- 渲染/布局只能在游戏内手动验证；提交前必须 `/W4 /sdl` 零警告通过

## 高危陷阱

- `hook/binkw32hack.h` 中 `jmp gFuncEntry[i * 4]` 的 `i * 4` 是 MSVC 内联汇编的字节偏移写法，改回 `gFuncEntry[i]` 会跳到错误导出
- `DllMain` 对不支持的游戏版本只跳过 `init()`，绝不返回 `FALSE`（否则游戏解析不到 binkw32 导入直接启动失败）
- 运行/部署需把原版 `binkw32.dll` 改名为 `binkw32_o.dll` 留在游戏目录（代理会 LoadLibrary 它并转发 72 个导出）
- 游戏版本由 PE 入口点识别（`dllmain.cpp`），Hook 地址硬编码在 `H3FontExtension.cpp` 的 `init()`（如文本绘制 `0x4B51F0`）。改动地址前先对照 `idb/` 下本机 IDA 数据库（`H3.exe.i64`、`BINKW32.dll`）
- `H3FontExt` 是 `font` + 8 字节，通过补丁 `0x55B9CE` 处的分配大小注入；不要改变其字段布局
- `0x5FA22B` 处字节 == 4 或 HD Mod 32 位调色板 ⇒ 32 位色深；新增渲染代码必须走既有 16/32 位模板双路径，勿写死色深
- XP 模式启用 `/Zc:threadSafeInit-`：不要引入依赖 Vista+ API 的静态初始化
- Hook 内布局/测量缓存均为无锁静态数据，假设游戏主线程单线程调用；勿引入锁或无限增长缓存

## 结构

- `dllmain.cpp`：BinkW32 代理入口 + 版本识别；`hook/binkw32hack.h`：导出转发 stub
- `H3FontExtension.cpp`：全局状态、字体加载/DDraw 生命周期 Hook、`init()` 注册全部 Hook；`H3FontExtensionInternal.h`：跨 TU 共享声明
- `TextLayout.cpp`：颜色码解析、拆行、布局/测量缓存与全部宽度/行数 Hook；`TextRender.cpp`：16/32 位像素混合、字形绘制、`DrawBoundedString` Hook
- `ExtFont.*`：GBK 区/位码→wchar 查表、字形/宽度/阴影掩膜缓存、1MB 分片字形池
- `GlyphRasterizer*.cpp`：光栅化后端，Win7+ DirectWrite，XP 回退 GDI
- `Log.*`：写入 DLL 同目录 `H3CN.log`（UTF-8），替代 MessageBox 弹窗
- `src/H3CN.toml`：游戏从插件 DLL 同目录读取的运行时配置；新增配置项需同时改 `init()` 解析与 toml 默认值
- `tests/`：不依赖游戏的纯逻辑测试（独立 exe + ctest）；新增纯函数时优先补测试
- `cmake/WindowsXP.cmake`：VC-LTL + YY-Thunks 固定版本集成；`cmake/CopyIfNotExists.cmake`：仅目标不存在时复制（部署默认 toml）

## 约定

- 命名贴合 NH3API 风格：全局 `g` + PascalCase（`gPatcher`）、自研自由函数 camelCase（`splitTextIntoLines`）、类型/成员函数 PascalCase、自研成员变量 snake_case（`line_spacing`）；Hook 入口保留 `H3Font_` 前缀便于与游戏函数对照
- 所有源码与 `H3CN.toml` 必须 UTF-8（toml 无 BOM），编译带 `/utf-8`；文件 CRLF、4 空格缩进
- 注释用中文 + Doxygen `/** */`；禁止 `using namespace std`，用显式 `std::`
- 提交信息用中文，风格见 `git log`
