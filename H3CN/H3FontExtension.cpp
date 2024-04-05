#include "H3FontExtension.h"

using namespace h3;
using namespace std;

namespace H3FontExtension
{
    static DWORD __fastcall GetColor16(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.color[colorIdx].Value();
    }

    static DWORD __fastcall GetColor32(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.palette32->colors[colorIdx];
    }

    DWORD(__fastcall* GetColor)(const H3BasePalette565& palette, int colorIdx);

    static void __fastcall DrawPixcel16(const PUINT8 rowBuffer, int col, DWORD color)
    {
        *((WORD*)rowBuffer + col) = (WORD)color;
    }

    static void __fastcall DrawPixcel32(const PUINT8 rowBuffer, int col, DWORD color)
    {
        *((DWORD*)rowBuffer + col) = color;
    }

    void(__fastcall* DrawPixcel)(const PUINT8 rowBuffer, int col, DWORD color);

    /**
     * @brief 拆分文本为行
     * @param pFont ASCII字体
     * @param pStr 文本字符串
     * @param nWidth 文本框宽度
     * @param stringVector 拆分后的文本行容器
     * @return
     */
    static void __stdcall SplitTextIntoLines(H3FontExt* pFont, char* szText, const int iBoxWidth,
                                             vector<TextLineStruct>& lines)
    {
        // 获取空格宽度
        UINT32 spaceWidth = pFont->width[32].span + pFont->width[32].leftMargin + pFont->width[32].rightMargin;

        UINT32 lineWidth = 0;
        std::string strBuffer;

        while (*szText)
        {
            // 行首空格和换行符处理
            UINT32 blankWidth = 0;
            UINT32 blankCount = 0;
            for (UINT8 code = *szText; code == ' ' || code == '\n'; code = *++szText)
            {
                if (code == ' ')
                {
                    blankWidth += spaceWidth;
                    ++blankCount;
                    continue;
                }

                lines.push_back(TextLineStruct{strBuffer, lineWidth});
                strBuffer.clear();
                lineWidth = 0;
                blankWidth = 0;
                blankCount = 0;
                //++szText; // 换行时额外移动一格光标
            }

            // 通过空格或换行符取词，一个汉字算作一个词
            int wordWidth = 0;
            char* wordCursor = szText;
            for (UINT8 code = *wordCursor; *wordCursor != '\0'; code = *++wordCursor)
            {
                if (code == ' ' || code == '\n')
                {
                    break;
                }

                UINT8 extCode = *(wordCursor + 1);
                if (code >= DBCS_SECTION && extCode && extCode != 0xFF && extCode >= DBCS_POSITION)
                {
                    if (wordWidth == 0)
                    {
                        wordWidth = pFont->ExtData->GlyphWidth;
                        wordCursor += 2;
                    }
                    break;
                }

                // BUGFIX: 原版里词宽计算了颜色字符
                if (code == '{' || code == '}')
                {
                    continue;
                }

                wordWidth += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
            };

            // 当前的 句宽+取到的词宽+空白宽度 大于文本框宽度
            if (lineWidth + wordWidth + blankWidth > iBoxWidth)
            {
                // 判断句宽进行换行
                if (lineWidth > 0)
                {
                    lines.push_back(TextLineStruct{strBuffer, lineWidth});
                    strBuffer.clear();
                    lineWidth = 0;
                }
                blankCount = 0; // 移除行首空格
                blankWidth = 0; // 重置空格宽度

                // 如果取词的宽超过过了文本框的宽
                // 强行断行，将词拆行显示直到词宽，
                // 小于文本边框宽度
                while (wordWidth > iBoxWidth && *wordCursor != '\0')
                {
                    for (UINT8 code = *wordCursor; *wordCursor != '\0'; code = *++wordCursor)
                    {
                        if (code == ' ' || code == '\n')
                        {
                            break;
                        }

                        int charWidth =
                            pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                        if (charWidth + lineWidth > iBoxWidth)
                        {
                            break;
                        }

                        strBuffer.push_back(code);
                        lineWidth += charWidth;
                        wordWidth -= charWidth;
                    };

                    lines.push_back(TextLineStruct{strBuffer, lineWidth});
                    strBuffer.clear();
                    lineWidth = 0;
                }
            }

            // 插入词后空格
            strBuffer.append(blankCount, ' ');

            // 将词填入句末
            if (szText != wordCursor)
            {
                strBuffer.append(szText, wordCursor - szText);
                szText = wordCursor;
            };

            // 句宽增加词的宽度
            lineWidth += wordWidth;
            // 句宽增加空格宽度
            lineWidth += blankWidth;
        }

        // 文本剩余单独成句
        if (lineWidth > 0)
        {
            lines.push_back(TextLineStruct{strBuffer, lineWidth});
        }

        strBuffer.clear();
    }

    /**
     * @brief 绘制字符
     * @tparam T 彩色模式类型 仅支持 16位色、32位色
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param pOutputPcx 图像输出
     * @param nCode1 字符编码高位
     * @param nCode2 字符编码低位
     * @param nX 绘制位置左上角X坐标
     * @param nY 绘制位置左上角Y坐标
     * @param nFontColor RGB颜色码
     * @param nShadowColor 阴影RGB颜色码
     * @return
     */
    static bool __fastcall H3Font_DrawChar(H3FontExt* pFont, H3LoadedPcx16* pOutputPcx, uint8_t cHiCode,
                                           uint8_t cLoCode, int iX, int iY, DWORD uFontColor)
    {
        // 绘制英文文字
        if (cLoCode == 0)
        {
            PUINT8 pFontBuffer = pFont->GetChar(cHiCode);
            int startX = iX + pFont->width[cHiCode].leftMargin;
            int startY = iY;
            for (int rowIdx = 0; rowIdx < pFont->height; ++rowIdx)
            {
                for (int colIdx = 0; colIdx < pFont->width[cHiCode].span; ++colIdx)
                {
                    uint8_t nPixcel = *pFontBuffer++;
                    if (!nPixcel)
                    {
                        continue;
                    }

                    // 255表示绘制正常颜色，否则则绘制阴影
                    if (nPixcel == 255)
                    {
                        DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, uFontColor);
                    }
                    else
                    {
                        DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, ShadowColor);
                    }
                }
            }

            return false;
        }

        // 绘制汉字文字

        auto cFont = pFont->ExtData;
        // 左边距为1，对齐Y中轴
        int startX = iX + cFont->MarginLeft;
        int startY = iY;
        PUINT8 pFontFileBuffer = cFont->GetHzkCharacterPcxPointer(cHiCode, cLoCode);

        for (int rowIdx = 0; rowIdx < cFont->Height; ++rowIdx)
        {
            for (int colIdx = 0; colIdx < cFont->Width; ++colIdx)
            {
                uint8_t alpha = *(pFontFileBuffer + (cFont->Height * rowIdx + colIdx));
                if (alpha == 0)
                {
                    continue;
                }
                DrawPixcel(pOutputPcx->GetRow(startY + rowIdx), startX + colIdx, uFontColor);

                // 是否绘制阴影
                if (!cFont->DrawShadow)
                {
                    continue;
                }
                DrawPixcel(pOutputPcx->GetRow(startY + rowIdx + 1), startX + colIdx + 1, ShadowColor);
            }
        }

        return true;
    }

    /**
     * @brief 绘制文本行
     * @param pFont ASCII字体
     * @param pStr 文本字符串
     * @param pPcx 图像输出
     * @param nX 绘制字符位置左上角X坐标
     * @param nY 绘制字符位置左上角Y坐标
     * @param nWidth 文本框宽度
     * @param nHeight 文本框高度
     * @param nColorIdx 颜色序号，参考eTextColor定义
     * @param nAlignFlags 文本排版规则，参考eTextAlignment定义
     * @param nFontStyle 字体风格（无用）
     * @return
     */
    static void __stdcall H3Font_DrawText(HiHook* h, H3FontExt* pFont, char* szText, H3LoadedPcx16* pPcx, int iX,
                                          int iY, int iBoxWidth, int iBoxHeight, uint32_t uColorIdx,
                                          uint32_t uAlignFlags, int iFontStyle)
    {
        if (!*szText || iBoxWidth == 0)
        {
            return;
        }

        // 拆行计算各行长短
        vector<TextLineStruct> textLines;
        SplitTextIntoLines(pFont, szText, iBoxWidth, textLines);

        int startY = 0;
        // 垂直居中对齐
        if (uAlignFlags & eTextAlignment::VCENTER)
        {
            uAlignFlags &= ~eTextAlignment::VCENTER;
            int textTotalHeight = pFont->height * textLines.size(); // 文本总高度
            if (textTotalHeight >= iBoxHeight)
            {
                if (iBoxHeight < 2 * pFont->height)
                {
                    startY = (iBoxHeight - pFont->height) / 2;
                }
            }
            else
            {
                startY = (iBoxHeight - textTotalHeight) / 2;
            }
        }
        // 垂直底部对齐
        if (uAlignFlags & eTextAlignment::VBOTTOM)
        {
            uAlignFlags &= ~eTextAlignment::VBOTTOM;
            int textTotalHeight = pFont->height * textLines.size(); // 文本总高度
            if (textTotalHeight < iBoxHeight)
            {
                startY = iBoxHeight - textTotalHeight;
            }
        }

        int cfontShift = std::max(0, (pFont->height - pFont->ExtData->Height) / 2);
        int cfontHeight = std::max(pFont->height, pFont->ExtData->Height);

        // 处理颜色代码
        uColorIdx = uColorIdx & 0x100 ? uColorIdx & 0xFE : uColorIdx + 9;

        DWORD defaultColor = GetColor(pFont->palette, uColorIdx);
        DWORD textColor = defaultColor;

        int rowIdx = 0;
        for (const TextLineStruct& p : textLines)
        {
            if (p.lineWidth == 0)
            {
                ++rowIdx;
                continue;
            }

            // 当前行绘制后溢出文本框则不绘制
            if (startY + (rowIdx + 1) * pFont->height > iBoxHeight)
            {
                break;
            }

            // 水平左右对齐
            int startX = 0;
            switch (uAlignFlags)
            {
            case 0:
                startX = 0;
                break;
            case 1:
                startX = (iBoxWidth - p.lineWidth) / 2;
                break;
            case 2:
                startX = iBoxWidth - p.lineWidth;
                break;
            }

            int posMove = 0;
            for (int i = 0; i < p.Text.length(); ++i)
            {
                uint8_t code = p.Text[i];

                if (code == '{')
                {
                    textColor = GetColor(pFont->palette, uColorIdx + 1);
                    continue;
                }

                if (code == '}')
                {
                    textColor = defaultColor;
                    continue;
                }

                if (code < DBCS_SECTION || code == 0xFF)
                {
                    H3Font_DrawChar(pFont, pPcx, code, 0, iX + startX + posMove, iY + startY + rowIdx * cfontHeight,
                                    textColor);
                    posMove += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                    continue;
                }

                UINT8 extCode = p.Text[i + 1];
                if (extCode && extCode != 0xFF && extCode >= DBCS_POSITION)
                {
                    H3Font_DrawChar(pFont, pPcx, code, extCode, iX + startX + posMove,
                                    iY + startY + cfontShift + rowIdx * cfontHeight, textColor);
                    posMove += pFont->ExtData->GlyphWidth;
                    ++i;
                }
            }

            // 推进行
            ++rowIdx;
        }
    }

    /**
     * @brief 拆分文本为行
     * @param pFont ASCII字体
     * @param pStr 文本字符串
     * @param nWidth 文本框宽度
     * @param stringVector 拆分后的文本行容器
     * @return
     */
    static void __stdcall H3Font_SplitTextIntoLines(HiHook* h, H3FontExt* pFont, char* szText, const int iBoxWidth,
                                                    H3Vector<H3String>& lines)
    {
        if (!strlen(szText))
        {
            return;
        }

        vector<TextLineStruct> vlines;
        SplitTextIntoLines(pFont, szText, iBoxWidth, vlines);
        for (auto& line : vlines)
        {
            lines.Add(line.Text.c_str());
        }
    }

    /**
     * @brief 获取文本中最长单词
     * @param h 函数钩
     * @param pFont 字体
     * @param szText 文本段
     * @return 最长的一个词
     */
    static int __stdcall H3Font_GetWordWidth(HiHook* h, H3FontExt* pFont, char* szText)
    {
        int strLength = strlen(szText);
        if (strLength <= 0)
            return 0;

        int maxWidth = 256;
        int wordWidth = 0;
        for (int i = 0; i < strLength; ++i)
        {
            UINT8 code = szText[i];
            if (code == ' ' || code == '\n')
            {
                maxWidth = max(wordWidth, maxWidth);
                wordWidth = 0;
                continue;
            }

            if (code == '{' || code == '}')
            {
                continue;
            }

            // 单字节码
            if (code < DBCS_SECTION || code == 0xFF)
            {
                wordWidth += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                continue;
            }

            // 双字节码
            UINT8 extCode = szText[i + 1];
            if (extCode || extCode == 0xFF || extCode < DBCS_POSITION)
            {
                continue;
            }

            break;
        }

        return max(wordWidth, maxWidth);
    }

    /**
     * @brief 获取指定宽度下一段话中最长句
     * @param h
     * @param pFont
     * @param szText
     * @param iBoxWidth
     * @return
     */
    static int __stdcall H3Font_GetLineWrapWidth(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        int strLength = strlen(szText);
        if (strLength <= 0)
            return 0;

        int maxWidth = 0;
        int lineWidth = 0; // 当前行行宽
        int charWidth = 0; // 当前行字宽
        int charSize = 0;  // 字符占用字节

        // 遍历字符串
        for (int i = 0; i < strLength; ++i)
        {
            // 当前文本代码
            UINT8 code = szText[i];

            // 结束符直接结束遍历
            if (!code)
            {
                break;
            }

            // 换行符比较后记录最大行宽，重置当前行宽
            if (code == '\n')
            {
                maxWidth = max(lineWidth, maxWidth);
                lineWidth = 0;
                continue;
            }

            // 颜色标记不记录行宽
            if (code == '{' || code == '}')
            {
                continue;
            }

            charSize = 0;  // 字符字节数
            charWidth = 0; // 重置字符宽度

            // 单字节码
            if (code < DBCS_SECTION || code == 0xFF)
            {
                charWidth = pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                charSize = 1; // 单字节
            }
            else
            {
                // 单字节码
                UINT8 extCode = szText[i + 1];
                if (extCode && extCode != 0xFF && extCode >= DBCS_POSITION)
                {
                    charWidth = pFont->ExtData->GlyphWidth;
                    charSize = 2; // 双字节
                    ++i;          // 双字节跳过高位字符
                }
            }

            if (lineWidth + charWidth > iBoxWidth)
            {
                maxWidth = max(lineWidth, maxWidth); // 判断长度
                lineWidth = 0;                       // 重置句长度
                i -= charSize;                       // 超长则倒回去
            }
            else
            {
                lineWidth += charWidth;
            }
        }

        return max(lineWidth, maxWidth);
    }

    /**
     * @brief 获取一段话的行数
     * @param h
     * @param pFont
     * @param szText
     * @param iWidth
     * @return
     */
    static int __stdcall H3Font_GetLineCount(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        int strLength = strlen(szText);
        if (strLength <= 0)
            return 0;

        int lineCount = 1; // 至少一行
        int lineWidth = 0; // 当前行行宽
        int charWidth = 0; // 当前行字宽
        int charSize = 0;  // 字符占用字节

        // 遍历字符串
        for (int i = 0; i < strLength; ++i)
        {
            // 当前文本代码
            UINT8 code = szText[i];

            // 结束符直接结束遍历
            if (!code)
            {
                break;
            }

            // 换行符比较后记录最大行宽，重置当前行宽
            if (code == '\n')
            {
                ++lineCount;
                lineWidth = 0;
                continue;
            }

            // 颜色标记不记录行宽
            if (code == '{' || code == '}')
            {
                continue;
            }

            charSize = 0;  // 字符字节数
            charWidth = 0; // 重置字符宽度

            // 单字节码
            if (code < DBCS_SECTION || code == 0xFF)
            {
                charWidth = pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                charSize = 1; // 单字节
            }
            else
            {
                // 双字节码
                UINT8 extCode = szText[i + 1];
                if (extCode && extCode != 0xFF && extCode >= DBCS_POSITION)
                {
                    charWidth = pFont->ExtData->GlyphWidth;
                    charSize = 2; // 双字节
                    ++i;          // 双字节跳过高位字符
                }
            }

            if (lineWidth + charWidth <= iBoxWidth)
            {
                lineWidth += charWidth;
                continue;
            }

            ++lineCount;   // 行数+1
            lineWidth = 0; // 重置句长度
            i -= charSize; // 超长则倒回去
        }

        return lineCount;
    }

    /**
     * @brief 获取一段话中最长的句子
     * @param h
     * @param pFont
     * @param szText
     * @return
     */
    static int __stdcall H3Font_GetLineWidth(HiHook* h, H3FontExt* pFont, char* szText)
    {
        int strLength = strlen(szText);
        if (strLength <= 0)
            return 0;

        int maxWidth = 0;
        int lineWidth = 0;
        for (int i = 0; i < strLength; ++i)
        {
            UINT8 code = szText[i];
            if (code == '\n')
            {
                maxWidth = max(lineWidth, maxWidth);
                lineWidth = 0;
                continue;
            }

            if (code == '{' || code == '}')
            {
                continue;
            }

            // 单字节码
            if (code < DBCS_SECTION || code == 0xFF)
            {
                lineWidth += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                continue;
            }

            // 获取位码
            UINT8 extCode = szText[i + 1];
            if (!extCode || extCode == 0xFF || extCode < DBCS_POSITION)
            {
                continue;
            }

            lineWidth += pFont->ExtData->GlyphWidth;
            ++i;
        }

        return max(lineWidth, maxWidth);
    }

    /**
     * @brief 字体加载拓展
     * @param h
     * @param name 字体名称
     * @return
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
        return font;
    }

    /**
     * @brief DDraw初始化拓展
     * @param h
     * @return
     */
    static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
    {
        FASTCALL_0(void, h->GetDefaultFunc());

        // 根据游戏的图像模式初始化图像渲染
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
    }

    /**
     * @brief 插件配置初始化
     * @return 初始化状态
     */
    bool Init()
    {
#ifndef NDEBUG
        MessageBoxW(H3Hwnd::Get(), L"注入成功", L"调试中", 0);
#endif
        // 获取代码修补库
        _P = GetPatcher();
        _PI = _P->CreateInstance("HD.Plugin.H3FontExtension");

        // 加载配置
        try
        {
            auto config = toml::parse_file("H3CN.toml");

            toml::array fontArr = *config["Fonts"].as_array();
            for (const auto& item : fontArr)
            {
                const auto& fontCfg = item.as_table();
                LPCSTR fntName = _strlwr((char*)fontCfg->get("Name")->value_or(""));
                g_ExtFontTable[fntName] =
                    ExtFont(fntName, fontCfg->get("ExtFont")->value_or(""), fontCfg->get("Height")->value_or(0),
                            fontCfg->get("Width")->value_or(0), fontCfg->get("MarginLeft")->value_or(1),
                            fontCfg->get("MarginRight")->value_or(0), fontCfg->get("MarginBottom")->value_or(0),
                            fontCfg->get("DrawShadow")->value_or(true));
            }

            Cmpt_TextColor = config["General"]["TextColor"].value_or(true);
            if (Cmpt_TextColor)
            {
                auto configColor = toml::parse_file("H3CN.TextColor.toml");
                TextColorMap = *configColor["TextColor"].as_table();
            }

            // 文本行宽计算规则限制
            BoxWidthModify = config["MessageBox"]["BoxWidthModify"].value_or(0);
        }
        catch (const std::exception&)
        {
            MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
        }

        // 注入函数劫持

        // 挂载一个扩展函数在游戏DDraw初始化的位置 用于检测色彩模式
        _PI->WriteHiHook(0x601AB0, SPLICE_, FASTCALL_, EXTENDED_, Main_DirectDrawInit_Hook);

        // 扩展字体申请的堆大小，额外增加4bytes空间
        _PI->WriteDword(0x55B9CE + 1, H3FontExt::SIZE + 4);

        // 字体加载后填入拓展字符区
        _PI->WriteHiHook(0x55BD10, SPLICE_, THISCALL_, EXTENDED_, H3Font_Load_Hook);

        // 字符绘制和文本框宽度计算
        _PI->WriteHiHook(0x4B51F0, SPLICE_, THISCALL_, H3Font_DrawText);           // 文本绘制
        _PI->WriteHiHook(0x4B5580, SPLICE_, THISCALL_, H3Font_GetLineCount);       // 计算文本的行数
        _PI->WriteHiHook(0x4B56F0, SPLICE_, THISCALL_, H3Font_GetLineWidth);       // 最长文本行长度
        _PI->WriteHiHook(0x4B5770, SPLICE_, THISCALL_, H3Font_GetWordWidth);       // 最长单词长度
        _PI->WriteHiHook(0x4B57E0, SPLICE_, THISCALL_, H3Font_GetLineWrapWidth);   // 最长换行长度
        _PI->WriteHiHook(0x4B58F0, SPLICE_, THISCALL_, H3Font_SplitTextIntoLines); // 拆分文本行

        return true;
    }
} // namespace H3FontExtension
