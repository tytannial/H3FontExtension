#include "H3FontExtension.h"

using namespace h3;
using namespace std;

namespace H3FontExtension
{

// ============================================================
// Section 1: ExtFont 实现
// ============================================================

/**
 * @brief 构造 ExtFont 并立即加载字库文件
 *
 * 委托给 LoadHzhFont 完成实际的文件读取和属性设置。
 */
ExtFont::ExtFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight, int iMarginBottom,
    bool bDrawShadow/*, int lineHeightAdjust*/)
{
    LoadHzhFont(lpFileName, iHeight, iWidth, iMarginLeft, iMarginRight, iMarginBottom, bDrawShadow/*, lineHeightAdjust*/);
}

/**
 * @brief 从二进制文件加载扩展字库数据
 *
 * 读取 .hzh 格式的位图字库文件到内存。
 * 字库中每个汉字占用 Width×Height 字节，按 GBK 区码/位码索引。
 *
 * @return 始终返回 false（游戏引擎约定，避免触发原始字体加载逻辑）
 */
bool __fastcall ExtFont::LoadHzhFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight,
    int iMarginBottom, bool bDrawShadow/*, int lineHeightAdjust*/)
{
    std::ifstream file(lpFileName, std::ios::in | std::ios::binary);

    if (file.good() == false)
    {
        MessageBoxW(h3::H3Hwnd::Get(), L"初始化字体失败", L"错误", 0);
        return false;
    }

    this->Height = iHeight;
    this->Width = iWidth;
    this->MarginLeft = iMarginLeft;
    this->MarginRight = iMarginRight;
    this->MarginBottom = iMarginBottom;
    this->DrawShadow = bDrawShadow;
    this->GlyphWidth = iMarginLeft + iWidth + iMarginRight;
    //this->LineHeightAdjust = lineHeightAdjust;

    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    this->FontFileBuffer = new UINT8[fileSize];
    file.seekg(0, std::ios::beg);
    file.read((char*)this->FontFileBuffer, fileSize);

    return false;
}

// ============================================================
// Section 2: 渲染后端（色深适配）
// ============================================================
//
// 游戏支持 16 位（RGB565）和 32 位（RGB888）两种色深模式。
// 通过函数指针在 DDraw 初始化时动态绑定对应实现。

/** @brief 16 位色深：从调色板获取颜色 */
static DWORD __fastcall GetColor16(const H3BasePalette565& palette, int colorIdx)
{
    return palette.color[colorIdx].Value();
}

/** @brief 32 位色深：从调色板获取颜色 */
static DWORD __fastcall GetColor32(const H3BasePalette565& palette, int colorIdx)
{
    return palette.palette32->colors[colorIdx];
}

/** @brief 色深自适应：获取调色板颜色的函数指针 */
DWORD(__fastcall* GetColor)(const H3BasePalette565& palette, int colorIdx);

/** @brief 16 位色深：写入一个像素到行缓冲区 */
static void __fastcall DrawPixcel16(const PUINT8 rowBuffer, int col, DWORD color)
{
    *((WORD*)rowBuffer + col) = (WORD)color;
}

/** @brief 32 位色深：写入一个像素到行缓冲区 */
static void __fastcall DrawPixcel32(const PUINT8 rowBuffer, int col, DWORD color)
{
    *((DWORD*)rowBuffer + col) = color;
}

/** @brief 色深自适应：绘制像素的函数指针 */
void(__fastcall* DrawPixcel)(const PUINT8 rowBuffer, int col, DWORD color);

// ============================================================
// Section 3: 文本扫描与处理
// ============================================================

/**
 * @brief 遍历文本中所有"可见 token"，自动跳过颜色码 {~RRGGBB} / { } / }
 *
 * 颜色码格式：
 *   - {~#RRGGBB}  十六进制颜色（或 {~colorname} 命名颜色）
 *   - { / }        高亮切换（{ 进入高亮，} 恢复默认）
 *
 * 这些控制字符在遍历中被跳过，回调只接收实际可见字符。
 *
 * @param pFont   字体指针
 * @param szText  输入文本（以 '\0' 结尾）
 * @param fn      回调：fn(TokenType type, int width, int charBytes) -> bool
 *                返回 false 可提前终止遍历
 * @return        遍历结束时的指针位置（调用方可用于恢复 szText 推进）
 */
template<typename Func>
static const char* ForEachVisibleChar(H3FontExt* pFont, LPCSTR szText, Func&& fn)
{
    const auto* widthArr = pFont->width;
    const int spaceWidth = GetH3CharWidth(widthArr, 32); // 空格宽度
    const int glyphWidth = pFont->ExtData->GlyphWidth;    // 双字节字符宽度

    while (*szText)
    {
        uint8_t code = static_cast<uint8_t>(*szText);

        // -------- 跳过颜色码 --------
        if (IsTextColorEnable && (code == '{' || code == '}'))
        {
            if (code == '{' && *(szText + 1) == '~')
            {
                szText += 2; // 跳过 "{~"
                while (*szText && *szText != '}' && *szText != ' ' && *szText != '\n')
                    ++szText;
                if (*szText == '}') // 跳过 '}'
                    ++szText;
                continue;
            }
            ++szText;
            continue;
        }

        // -------- 换行 --------
        if (code == '\n')
        {
            if (!fn(TokenType::Newline, 0, 1))
                return szText;
            ++szText;
            continue;
        }

        // -------- 空格 --------
        if (code == ' ')
        {
            if (!fn(TokenType::Space, spaceWidth, 1))
                return szText;
            ++szText;
            continue;
        }

        // -------- 单字节字符 --------
        if (IsSingleByte(code))
        {
            if (!fn(TokenType::SingleByte, GetH3CharWidth(widthArr, code), 1))
                return szText;
            ++szText;
            continue;
        }

        // -------- 双字节字符 --------
        uint8_t nc = static_cast<uint8_t>(*(szText + 1));
        if (IsDBCSLeadByte(code, nc))
        {
            if (!fn(TokenType::DoubleByte, glyphWidth, 2))
                return szText;
            szText += 2;
        }
        else
        {
            ++szText; // 无效的双字节首字节，跳过
        }
    }
    return szText;
}

/**
 * @brief 将一行原始文本（含颜色码）预处理为 CleanLine
 *
 * 剥离所有颜色控制字符 {~...} / { / }，生成纯可见字符序列，
 * 同时将颜色变更信息记录为 ColorStop 列表。
 *
 * @param src            原始拆行结果（含颜色码）
 * @param defaultColor   默认渲染颜色（当前色深格式）
 * @param highlightColor { } 切换的高亮颜色
 * @param is32bit        当前是否为 32 位色深模式
 * @param dst            [out] 预处理结果
 */
static void PreprocessLine(const TextLineStruct& src, DWORD defaultColor, DWORD highlightColor, bool is32bit, CleanLine& dst)
{
    dst.lineWidth = src.lineWidth;
    dst.text.clear();
    dst.stops.clear();

    if (src.lineWidth == 0)
        return;

    const std::string& raw = src.Text;
    const size_t len = raw.size();

    // 预分配：clean text 最多和 raw 一样长，通常更短
    dst.text.reserve(len);

    DWORD curColor = defaultColor;

    for (size_t i = 0; i < len; )
    {
        const uint8_t code = static_cast<uint8_t>(raw[i]);

        // ============ '{' —— 进入高亮或自定义颜色 ============
        if (code == '{')
        {
            DWORD newColor = highlightColor;
            ++i;

            if (IsTextColorEnable && i < len && raw[i] == '~')
            {
                // 跳过 '~'，收集颜色码
                ++i;
                const size_t codeStart = i;
                while (i < len && raw[i] != '}')
                    ++i;

                if (i > codeStart)
                {
                    if (raw[codeStart] == '#')
                    {
                        // #RRGGBB 格式：直接解析为十六进制整数
                        DWORD c = 0;
                        auto [_, ec] = std::from_chars(
                            raw.data() + codeStart + 1,
                            raw.data() + i,
                            c, 16);
                        newColor = (ec == std::errc()) ? c : defaultColor;
                    }
                    else
                    {
                        // 命名颜色：从配置表查询，16 位模式需转 RGB565
                        std::string key(raw.data() + codeStart, i - codeStart);
                        newColor = TextColorMap[key].value_or(defaultColor);
                        if (newColor != defaultColor && !is32bit)
                            newColor = RGB888toRGB565(newColor);
                    }
                }
                else
                {
                    newColor = defaultColor;
                }

                // 跳过 '}'
                if (i < len && raw[i] == '}')
                    ++i;
            }

            // 颜色有变化才记录 Stop
            if (newColor != curColor)
            {
                curColor = newColor;
                dst.stops.push_back({ dst.text.size(), curColor });
            }
            continue;
        }

        // ============ '}' —— 恢复默认颜色 ============
        if (code == '}')
        {
            if (curColor != defaultColor)
            {
                curColor = defaultColor;
                dst.stops.push_back({ dst.text.size(), curColor });
            }
            ++i;
            continue;
        }

        // ============ 可见字符：拷贝到 clean text ============
        if (IsSingleByte(code))
        {
            dst.text.push_back(static_cast<char>(code));
            ++i;
        }
        else
        {
            // 双字节字符：先检查下一字节是否存在且有效
            if (i + 1 < len)
            {
                const uint8_t nextCode = static_cast<uint8_t>(raw[i + 1]);
                if (IsDBCSLeadByte(code, nextCode))
                {
                    dst.text.push_back(raw[i]);
                    dst.text.push_back(raw[i + 1]);
                    i += 2;
                    continue;
                }
            }
            // 无效双字节或尾部不完整，跳过首字节
            ++i;
        }
    }
}

/**
 * @brief 将文本按指定宽度拆分为多行
 *
 * 算法流程（按词为单位）：
 *   1. 跳过行首空白和换行符
 *   2. 用 ForEachVisibleChar 确定下一个"词"的边界
 *   3. 若当前行放不下该词，输出当前行
 *   4. 若词本身超宽，逐字符强制拆行
 *   5. 将词追加到当前行
 *
 * @param pFont     ASCII 字体指针
 * @param szText    输入文本（以 '\0' 结尾）
 * @param iBoxWidth 文本框像素宽度
 * @param lines     [out] 拆分结果（含颜色码的原始行）
 */
static void __stdcall SplitTextIntoLines(H3FontExt* pFont, LPCSTR szText, const int iBoxWidth,
    vector<TextLineStruct>& lines)
{
    if (!*szText)
        return;

    // 缓存常用参数，避免循环中反复解引用
    const auto* widthArr = pFont->width;
    const int spaceWidth = GetH3CharWidth(widthArr, 32);
    const int glyphWidth = pFont->ExtData->GlyphWidth;

    std::string lineBuf;
    int lineWidth = 0;

    // ============ 主循环：按词为单位处理 ============
    while (*szText)
    {
        // ---- 阶段 1：处理行首空白和换行符 ----
        int blankCount = 0;
        int blankWidth = 0;

        while (true)
        {
            uint8_t ch = static_cast<uint8_t>(*szText);
            if (ch == ' ')
            {
                blankWidth += spaceWidth;
                ++blankCount;
                ++szText;
            }
            else if (ch == '\n')
            {
                // 换行：当前行强制输出，重置状态
                lines.push_back({ std::move(lineBuf), lineWidth });
                lineBuf.clear();
                lineWidth = 0;
                blankCount = 0;
                blankWidth = 0;
                ++szText;
            }
            else
            {
                break;
            }
        }

        if (!*szText)
            break;

        // ---- 阶段 2：取词——使用 ForEachVisibleChar 确定词边界 ----
        int wordWidth = 0;
        const char* wordStart = szText;
        const char* wordEnd = szText;

        const char* realEnd = ForEachVisibleChar(pFont, szText,
            [&](TokenType type, int width, int charBytes)
            {
                if (type == TokenType::Space || type == TokenType::Newline)
                    return false; // 遇到空格或换行，结束当前词的扫描

                wordWidth += width;
                wordEnd += charBytes;
                return true; // 继续扫描
            });

        // wordEnd 仅记录可见字符位置；若只消费了颜色码/花括号等非可见内容，
        // 则 wordEnd 未推进，导致外层 szText = wordEnd 陷入死循环。
        // 此时用 ForEachVisibleChar 返回的 realEnd 安全推进 szText。
        if (wordEnd == wordStart)
        {
            szText = (realEnd > wordStart) ? realEnd : (wordStart + 1);
            continue;
        }

        // ---- 阶段 3：拆行判定 ----
        if (lineWidth + wordWidth + blankWidth > iBoxWidth)
        {
            if (lineWidth > 0)
            {
                // 当前行已非空，先输出当前行
                lines.push_back({ std::move(lineBuf), lineWidth });
                lineBuf.clear();
                lineWidth = 0;
            }
            blankCount = 0;
            blankWidth = 0;

            // 如果词本身就超宽，逐字符拆行
            if (wordWidth > iBoxWidth)
            {
                LPCSTR p = wordStart;
                while (p < wordEnd)
                {
                    int charW = 0;
                    uint8_t code = static_cast<uint8_t>(*p);
                    // 安全获取下一字节：只有 wordEnd-p>=2 时才有可能为双字节
                    uint8_t nextCode = (wordEnd - p >= 2) ? static_cast<uint8_t>(*(p + 1)) : 0;
                    int charBytes = GetCharMetrics(code, nextCode, widthArr, glyphWidth, charW);

                    if (lineWidth + charW > iBoxWidth && lineWidth > 0)
                    {
                        lines.push_back({ std::move(lineBuf), lineWidth });
                        lineBuf.clear();
                        lineWidth = 0;
                    }

                    lineBuf.append(p, charBytes);
                    lineWidth += charW;
                    p += charBytes;
                }
                szText = wordEnd;
                continue; // 词已处理完毕，跳过阶段 4
            }
        }

        // ---- 阶段 4：将词追加到当前行 ----
        if (blankCount > 0)
            lineBuf.append(blankCount, ' ');

        // 仅追加可见字符（颜色码已在 ForEachVisibleChar 中被跳过）
        lineBuf.append(wordStart, static_cast<size_t>(wordEnd - wordStart));
        lineWidth += wordWidth + blankWidth;
        szText = wordEnd;
    }

    // 输出最后一行
    if (lineWidth > 0 || !lineBuf.empty())
    {
        lines.push_back({ std::move(lineBuf), lineWidth });
    }
}

// ============================================================
// Section 4: 字符 & 文本渲染
// ============================================================

/**
 * @brief 绘制单个字符到 PCX 缓冲区
 *
 * 支持两种模式：
 *   - cLoCode == 0：ASCII 模式，从 H3Font 原始字库取字形
 *   - cLoCode != 0：双字节模式，从 ExtFont 扩展字库取字形
 *
 * 阴影绘制逻辑：
 *   - ASCII：字形数据中值 255 为前景色，其他非零值为阴影
 *   - 双字节：alpha > 0 的像素绘制前景色，右下偏移 1 像素绘制阴影
 *
 * @param pFont       字体指针
 * @param pOutputPcx  目标 PCX 图像缓冲区
 * @param cHiCode     字符编码高字节（ASCII 时为字符码，双字节时为区码）
 * @param cLoCode     字符编码低字节（0 = ASCII 模式，非 0 = 双字节位码）
 * @param iX          绘制起点 X 坐标
 * @param iY          绘制起点 Y 坐标
 * @param uFontColor  前景色（已转换为当前色深格式）
 * @return true = 双字节字符，false = ASCII 字符
 */
static bool __fastcall H3Font_DrawChar(H3FontExt* pFont, H3LoadedPcx16* pOutputPcx, uint8_t cHiCode,
    uint8_t cLoCode, int iX, int iY, DWORD uFontColor)
{
    // ---- 绘制英文文字 ----
    if (cLoCode == 0)
    {
        PUINT8 pFontBuffer = pFont->GetChar(cHiCode);
        int startX = iX + pFont->width[cHiCode].leftMargin;
        int startY = iY;
        for (int rowIdx = 0; rowIdx < pFont->oriHeight; ++rowIdx)
        {
            for (int colIdx = 0; colIdx < pFont->width[cHiCode].span; ++colIdx)
            {
                uint8_t nPixcel = *pFontBuffer++;
                if (!nPixcel)
                    continue;

                // 255 表示绘制正常颜色，否则绘制阴影
                if (nPixcel == 255)
                    DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, uFontColor);
                else
                    DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, ShadowColor);
            }
        }
        return false;
    }

    // ---- 绘制汉字文字 ----
    auto cFont = pFont->ExtData;
    PUINT8 pFontFileBuffer = cFont->GetExtGlyphDataPtr(cHiCode, cLoCode);
    if (!pFontFileBuffer)
        return false;

    int startX = iX + cFont->MarginLeft;
    int startY = iY;

    for (int rowIdx = 0; rowIdx < cFont->Height; ++rowIdx)
    {
        for (int colIdx = 0; colIdx < cFont->Width; ++colIdx)
        {
            uint8_t alpha = *(pFontFileBuffer + (cFont->Height * rowIdx + colIdx));
            if (alpha == 0)
                continue;

            DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, uFontColor);

            if (!cFont->DrawShadow)
                continue;
            DrawPixcel(pOutputPcx->GetRow(startY + rowIdx + 1), startX + colIdx + 1, ShadowColor);
        }
    }

    return true;
}

/**
 * @brief 在指定矩形区域内绘制多行文本
 *
 * 完整渲染管线：
 *   1. SplitTextIntoLines  → 拆行
 *   2. 计算垂直对齐偏移
 *   3. PreprocessLine      → 逐行剥离颜色码
 *   4. 逐行逐字符绘制（含颜色切换）
 *
 * @param h           Hook 句柄（未使用）
 * @param pFont       字体指针
 * @param szText      输入文本（含颜色码和控制字符）
 * @param pPcx        目标 PCX 图像缓冲区
 * @param iX          文本框左上角 X 坐标
 * @param iY          文本框左上角 Y 坐标
 * @param iBoxWidth   文本框像素宽度
 * @param iBoxHeight  文本框像素高度
 * @param uColorIdx   颜色索引（游戏调色板索引）
 * @param uAlignFlags 对齐标志（eTextAlignment 位掩码）
 * @param iFontStyle  字体样式（未使用）
 */
static void __stdcall H3Font_DrawText(HiHook* h, H3FontExt* pFont, char* szText, H3LoadedPcx16* pPcx,
    int iX, int iY, int iBoxWidth, int iBoxHeight,
    uint32_t uColorIdx, uint32_t uAlignFlags, int iFontStyle)
{
    if (!*szText || iBoxWidth == 0)
        return;

    // ========== 阶段一：拆行 ==========
    vector<TextLineStruct> textLines;
    SplitTextIntoLines(pFont, szText, iBoxWidth, textLines);
    if (textLines.empty())
        return;

    const int lineCount = static_cast<int>(textLines.size());

    // ========== 阶段二：缓存字体度量 ==========
    const int fontHeight = pFont->height;
    const int oriHeight = pFont->oriHeight;
    const int extHeight = pFont->ExtData->Height;
    const int glyphWidth = pFont->ExtData->GlyphWidth;
    const auto* widthArr = pFont->width;

    const int ascShift = (fontHeight - oriHeight) / 2;   // ASCII 字符垂直偏移
    const int extShift = (fontHeight - extHeight) / 2;   // 双字节字符垂直偏移

    // ========== 阶段三：解析颜色 ==========
    const uint32_t ci = (uColorIdx & 0x100) ? (uColorIdx & 0xFE) : (uColorIdx + 9);
    const DWORD defaultColor = GetColor(pFont->palette, ci);
    const DWORD highlightColor = GetColor(pFont->palette, ci + 1);
    const bool is32bit = (H3BitMode::Get() == 4);

    // ========== 阶段四：垂直对齐 ==========
    int startY = 0;

    if (uAlignFlags & eTextAlignment::VCENTER)
    {
        uAlignFlags &= ~eTextAlignment::VCENTER;
        const int totalH = fontHeight * lineCount;
        if (totalH >= iBoxHeight)
        {
            if (iBoxHeight < 2 * fontHeight)
                startY = (iBoxHeight - fontHeight) / 2;
        }
        else
        {
            startY = (iBoxHeight - totalH) / 2;
        }
    }
    else if (uAlignFlags & eTextAlignment::VBOTTOM)
    {
        uAlignFlags &= ~eTextAlignment::VBOTTOM;
        const int totalH = fontHeight * lineCount;
        if (totalH < iBoxHeight)
            startY = iBoxHeight - totalH;
    }

    // ========== 阶段五：预处理所有行（剥离颜色码 + 预解析颜色）==========
    std::vector<CleanLine> cleanLines;
    cleanLines.reserve(lineCount);

    for (int i = 0; i < lineCount; ++i)
    {
        cleanLines.emplace_back();
        PreprocessLine(textLines[i], defaultColor, highlightColor, is32bit, cleanLines.back());
    }

    // ========== 阶段六：逐行绘制 ==========
    const int bottomBound = iY + iBoxHeight;

    for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
    {
        const CleanLine& line = cleanLines[rowIdx];
        if (line.lineWidth == 0)
            continue;

        const int lineY = iY + startY + rowIdx * fontHeight;
        if (lineY + fontHeight > bottomBound)
            break;

        // 水平对齐
        int startX = 0;
        switch (uAlignFlags)
        {
        case 1: startX = (iBoxWidth - line.lineWidth) / 2; break; // 居中
        case 2: startX = iBoxWidth - line.lineWidth;         break; // 右对齐
        }

        int curX = iX + startX;
        const char* p = line.text.data();
        const char* end = p + line.text.size();

        // 颜色跟踪（无颜色变更时 stops 为空，全程用 defaultColor）
        DWORD curColor = defaultColor;
        size_t stopIdx = 0;
        const size_t stopCount = line.stops.size();

        while (p < end)
        {
            // 检查当前位置是否有颜色变更
            if (stopIdx < stopCount)
            {
                const size_t bytePos = static_cast<size_t>(p - line.text.data());
                if (line.stops[stopIdx].bytePos == bytePos)
                {
                    curColor = line.stops[stopIdx].color;
                    ++stopIdx;
                }
            }

            const uint8_t code = static_cast<uint8_t>(*p);

            if (IsSingleByte(code))
            {
                H3Font_DrawChar(pFont, pPcx, code, 0,
                    curX, lineY + ascShift, curColor);
                curX += GetH3CharWidth(widthArr, code);
                ++p;
            }
            else
            {
                // 双字节：确保下一字节存在
                uint8_t nextCode = (end - p >= 2) ? static_cast<uint8_t>(p[1]) : 0;
                if (IsDBCSLeadByte(code, nextCode))
                {
                    H3Font_DrawChar(pFont, pPcx, code, nextCode,
                        curX, lineY + extShift, curColor);
                    curX += glyphWidth;
                    p += 2;
                }
                else
                {
                    // 无效双字节，当单字节处理
                    H3Font_DrawChar(pFont, pPcx, code, 0,
                        curX, lineY + ascShift, curColor);
                    curX += GetH3CharWidth(widthArr, code);
                    ++p;
                }
            }
        }
    }
}

// ============================================================
// Section 5: Hook 包装函数
// ============================================================
//
// 以下函数是对游戏引擎原始字体接口的 Hook 替换。
// 签名与原始函数一致（__stdcall，首个参数为 HiHook*），
// 内部使用 H3FontExt 扩展字体实现中文支持。

/**
 * @brief Hook: 将文本拆分为多行（返回 H3Vector<H3String>）
 *
 * 替换游戏原始的文本拆分逻辑，支持双字节字符和颜色码。
 */
static void __stdcall H3Font_SplitTextIntoLines(HiHook* h, H3FontExt* pFont, char* szText, const int iBoxWidth,
    H3Vector<H3String>& lines)
{
    lines.RemoveAll();

    if (!szText || !*szText)
        return;

    vector<TextLineStruct> vlines;
    SplitTextIntoLines(pFont, szText, iBoxWidth, vlines);
    for (auto& line : vlines)
    {
        lines.Add(H3String(std::move(line.Text).c_str()));
    }
}

/**
 * @brief Hook: 计算文本中最长单词的宽度
 *
 * 用于消息框等需要根据内容自适应宽度的场景。
 * 结果受 BoxWidthMin / BoxWidthMax 约束。
 */
static int __stdcall H3Font_GetWordWidth(HiHook* h, H3FontExt* pFont, char* szText)
{
    if (!szText || !*szText)
        return 0;

    int maxWidth = 0;
    int wordWidth = 0;

    ForEachVisibleChar(pFont, szText,
        [&](TokenType type, int width, int /*bytes*/)
        {
            if (type == TokenType::Newline || type == TokenType::Space || type == TokenType::DoubleByte)
            {
                if (wordWidth > maxWidth)
                    maxWidth = wordWidth;
                wordWidth = 0;
            }
            else
            {
                wordWidth += width;
            }
            return true;
        });

    if (wordWidth > maxWidth)
        maxWidth = wordWidth;
    return clamp(maxWidth, BoxWidthMin, BoxWidthMax);
}

/**
 * @brief Hook: 计算文本在指定宽度下的实际换行后最大行宽
 *
 * 模拟换行行为，返回所有行中最宽一行的像素宽度。
 */
static int __stdcall H3Font_GetLineWrapWidth(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
{
    if (!szText || !*szText)
        return 0;

    int maxWidth = 0;
    int lineWidth = 0;

    ForEachVisibleChar(pFont, szText,
        [&](TokenType type, int width, int /*bytes*/)
        {
            if (type == TokenType::Newline)
            {
                if (lineWidth > maxWidth)
                    maxWidth = lineWidth;
                lineWidth = 0;
            }
            else
            {
                if (lineWidth + width > iBoxWidth)
                {
                    if (lineWidth > maxWidth)
                        maxWidth = lineWidth;
                    lineWidth = 0;
                }
                lineWidth += width;
            }
            return true;
        });

    if (lineWidth > maxWidth)
        maxWidth = lineWidth;
    return maxWidth;
}

/**
 * @brief Hook: 计算文本在指定宽度下的总行数
 */
static int __stdcall H3Font_GetLineCount(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
{
    if (!szText || !*szText)
        return 0;

    int lineCount = 1;
    int lineWidth = 0;

    ForEachVisibleChar(pFont, szText,
        [&](TokenType type, int width, int /*bytes*/)
        {
            if (type == TokenType::Newline)
            {
                ++lineCount;
                lineWidth = 0;
            }
            else
            {
                if (lineWidth + width > iBoxWidth)
                {
                    ++lineCount;
                    lineWidth = 0; // 当前字符归入新行
                }
                lineWidth += width;
            }
            return true;
        });

    return lineCount;
}

/**
 * @brief Hook: 计算文本中最长行的像素宽度（不换行）
 */
static int __stdcall H3Font_GetLineWidth(HiHook* h, H3FontExt* pFont, char* szText)
{
    if (!szText || !*szText)
        return 0;

    int maxWidth = 0;
    int lineWidth = 0;

    ForEachVisibleChar(pFont, szText,
        [&](TokenType type, int width, int /*bytes*/)
        {
            if (type == TokenType::Newline)
            {
                if (lineWidth > maxWidth)
                    maxWidth = lineWidth;
                lineWidth = 0;
            }
            else
            {
                lineWidth += width;
            }
            return true;
        });

    if (lineWidth > maxWidth)
        maxWidth = lineWidth;
    return maxWidth;
}

// ============================================================
// Section 6: 生命周期 Hook
// ============================================================

/**
 * @brief Hook: 字体加载后注入扩展字库
 *
 * 在游戏加载字体后，将对应的 ExtFont 挂载到 H3FontExt 上，
 * 并根据扩展字库高度调整字体行高。
 *
 * 字体名匹配规则：小写字体名精确匹配 → 回退到 "medfont.fnt"。
 */
static H3Font* __stdcall H3Font_Load_Hook(HiHook* h, char* name)
{
    auto fntName = _strlwr(name);
    auto font = FASTCALL_1(H3FontExt*, h->GetDefaultFunc(), fntName);
    font->ExtData = &g_ExtFontTable[fntName];
    if (!font->ExtData)
    {
        font->ExtData = &g_ExtFontTable.at("medfont.fnt");
    }
    font->oriHeight = font->height;
    font->height = std::max(font->height, font->ExtData->Height) /*+ font->ExtData->LineHeightAdjust*/;
    return font;
}

/**
 * @brief Hook: DirectDraw 初始化后配置渲染后端
 *
 * 根据游戏色深模式（16/32 位）绑定对应的 GetColor / DrawPixcel 实现。
 * 同时根据屏幕分辨率计算文本框宽度限制。
 */
static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
{
    FASTCALL_0(void, h->GetDefaultFunc());

    // 根据游戏的图像模式初始化图像渲染函数指针
    if (H3BitMode::Get() == 4)
    {
        GetColor = GetColor32;
        DrawPixcel = DrawPixcel32;
    }
    else
    {
        GetColor = GetColor16;
        DrawPixcel = DrawPixcel16;
    }

    auto maxWidth = H3GameWidth::Get() - 64 * 2;
    BoxWidthMax < 0 ? BoxWidthMax = maxWidth : BoxWidthMax = clamp(BoxWidthMax, 256, maxWidth);
    BoxWidthMin < 0 ? BoxWidthMin = maxWidth : BoxWidthMin = clamp(BoxWidthMin, 256, BoxWidthMax);
}

// ============================================================
// Section 7: 插件入口
// ============================================================

/**
 * @brief 插件初始化入口
 *
 * 执行流程：
 *   1. 获取 Patcher 实例
 *   2. 加载 H3CN.toml 配置文件（字体映射、颜色表、宽度限制）
 *   3. 注册所有 Hook（字体加载、文本渲染、布局计算）
 */
bool Init()
{
#ifndef NDEBUG
    MessageBoxW(H3Hwnd::Get(), L"注入成功", L"调试中", 0);
#endif

    // ---------- 获取 Patcher 实例 ----------
    _P = GetPatcher();
    _PI = _P->GetInstance("HD.Plugin.H3FontExtension");
    if (!_PI)
    {
        _PI = _P->CreateInstance("HD.Plugin.H3FontExtension");
    }
    else
    {
        _PI->UndoAll();
    }

    // ---------- 加载配置文件 ----------
    try
    {
        auto config = toml::parse_file("H3CN.toml");

        // 字体映射：[Fonts] 节，每个条目定义游戏字体名与扩展字库的对应关系
        toml::array fontArr = *config["Fonts"].as_array();
        for (const auto& item : fontArr)
        {
            const auto& fontCfg = item.as_table();
            g_ExtFontTable[_strlwr((char*)fontCfg->get("Name")->value_or(""))] =
                ExtFont(
                    fontCfg->get("ExtFont")->value_or("")
                    , fontCfg->get("Height")->value_or(0)
                    , fontCfg->get("Width")->value_or(0)
                    , fontCfg->get("MarginLeft")->value_or(1)
                    , fontCfg->get("MarginRight")->value_or(0)
                    , fontCfg->get("MarginBottom")->value_or(0)
                    , fontCfg->get("DrawShadow")->value_or(true)
                    //, fontCfg->get("LineHeightAdjust")->value_or(0)
                );
        }

        // 文本颜色：[General].TextColor 开关 + [TextColor] 命名颜色表
        IsTextColorEnable = config["General"]["TextColor"].value_or(true);
        if (IsTextColorEnable)
        {
            TextColorMap = *config["TextColor"].as_table();
        }

        // 消息框宽度限制：[MessageBox]
        BoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
        BoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
    }
    catch (const std::exception&)
    {
        MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
    }

    // ---------- 注册 Hook ----------

    // DDraw 初始化时检测色深模式
    _PI->WriteHiHook(0x601AB0, SPLICE_, FASTCALL_, EXTENDED_, Main_DirectDrawInit_Hook);

    // 扩展字体内存分配（H3Font → H3FontExt，+8 字节）
    _PI->WriteDword(0x55B9CE + 1, sizeof(H3FontExt));

    // 字体加载后挂载扩展字库
    _PI->WriteHiHook(0x55BAE0, SPLICE_, THISCALL_, EXTENDED_, H3Font_Load_Hook);

    // 文本渲染与布局 Hook
    _PI->WriteHiHook(0x4B51F0, SPLICE_, THISCALL_, H3Font_DrawText);           // 文本绘制
    _PI->WriteHiHook(0x4B5580, SPLICE_, THISCALL_, H3Font_GetLineCount);       // 计算文本行数
    _PI->WriteHiHook(0x4B56F0, SPLICE_, THISCALL_, H3Font_GetLineWidth);       // 最长文本行宽度
    _PI->WriteHiHook(0x4B5770, SPLICE_, THISCALL_, H3Font_GetWordWidth);       // 最长单词宽度
    _PI->WriteHiHook(0x4B57E0, SPLICE_, THISCALL_, H3Font_GetLineWrapWidth);   // 最长换行后宽度
    _PI->WriteHiHook(0x4B58F0, SPLICE_, THISCALL_, H3Font_SplitTextIntoLines); // 拆分文本行

    return true;
}

} // namespace H3FontExtension
