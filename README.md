# 关于
H3CN是针对与英雄无敌3完全版的汉语字体扩展插件，插件源码基于对于英雄无敌3可执行文件3.2的逆向工程，该项目仅仅基于个人兴趣爱好，您可以在MIT协议约束内自由使用该源码，但商用必须注明出处。您可以对编码规则进行修改，生成自己的字库来兼容其他语言编码。

# 实现原理
替换掉了原本程序中绘制字体和计算词、行宽度的函数。使用GBK点阵汉字字库绘制汉字。您可以使用多种注入工具注入该插件到游戏，推荐使用[HOMM3 HD](https://sites.google.com/site/heroes3hd/eng/download)。

# 功能
- 扩展游戏字体显示除原本的ASCII字符以外的所有GBK编码的所有汉字。兼容所有简体中文版英雄无敌3 3.1中所有用到的汉字。
- 支持绘制汉字阴影，使得原本的英雄无敌3 3.1中文版中汉字缺少的阴影得到实现。
- 动态计算消息框宽度，使得消息框能够根据游戏内实际显示的文本宽度进行扩展。原版3.1中文版中消息框被固定在了600宽度非常不美观。
- 允许自定义游戏中9种使用的字体对应的汉字字体。可以定义每种字体的宽高以及字符间距。
- 兼容32位色图像模式。

# 开发兼容
插件基于C++20和Visual Studio 2022/2026进行开发，使用CMake构建，使用到了部分C++20的特性。源码中还有不少可以优化的地方，希望能够有高人指点进一步优化插件性能。源码请提交时使用UTF8进行编码。

# 程序依赖

[NH3API](https://github.com/void2012/NH3API) 英雄无敌3程序内部数据结构 <br>
[Toml++](https://github.com/marzer/tomlplusplus) 配置文件读写 <br>

依赖由CMake统一管理（FetchContent固定版本），无需手动下载或解压。

# 构建

要求：Visual Studio 2022及以上（使用Visual Studio 17 2022/18 2026生成器）、CMake 3.24+，目标平台Win32（x86）。

```bat
:: 在线获取依赖（NH3API固定版本 + toml++ v3.4.0）
cmake --preset vs2026-x86
cmake --build --preset release

:: 复用本地NH3API源码（离线开发，路径见CMakePresets.json）
cmake --preset vs2026-x86-local
cmake --build --preset release-local
```

设置游戏目录后构建产物自动复制为 `BINKW32.DLL`/`BINKW32.PDB` 部署到游戏：

```bat
cmake --preset vs2026-x86 -DH3_PATH="D:/Games/HoMM3"
```

也可用 `-DFETCHCONTENT_SOURCE_DIR_NH3API=<路径>` 指向任意本地NH3API源码目录。

## Windows XP 兼容

默认构建产物兼容 Windows XP（SP3 x86）：通过 [VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)（CRT 统一链接系统msvcrt.dll）与 [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)（新SDK API的XP实现）实现，依赖由CMake自动下载并固定版本（VC-LTL v5.3.1、YY-Thunks v1.2.1）。

- 首次配置需联网下载依赖（约25MB）；离线或复用本地副本可指定 `-DH3CN_VC_LTL_DIR=<目录>` / `-DH3CN_YY_THUNKS_DIR=<目录>`
- 构建产物仅依赖 `KERNEL32/USER32/GDI32/msvcrt.dll`（与原始版本一致）
- 如仅需现代系统构建可关闭：`-DH3CN_TARGET_XP=OFF`

# 待实现内容
1. 拥有图形界面的字体生成配置工具。
2. 自定义字体行间距。
3. 拆行式文本输出支持跨行彩色文本。即彩色文本头和尾不用在同一行，影响位置地图、任务简报。
4. 与最新的[ERA](https://github.com/ethernidee/era)扩展兼容（当前仅兼容）（重要）
5. 兼容GOG的4.0原始可执行文件，该可执行文件重新进行过编译，需要重新适配。
6. 拆行美化，使得一些汉字中的标点符号出现位置更加合理。