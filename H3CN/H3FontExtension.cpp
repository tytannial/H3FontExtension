#include "H3FontExtension.h"

using namespace h3;
using namespace std;

namespace H3FontExtension
{
    /**
     * @brief 获取英文字体和汉字库字体映射
     * @param pFont 英文字体
     * @return 汉字字库字体
     */
    ExtFont* __fastcall GetMappedExtFont(H3Font* pFont)
    {
        auto bExtFontExisted = FontMap.contains(pFont);
        if (bExtFontExisted)
        {
            return FontMap[pFont];
        }

        for (size_t i = 0; i < 9; i++)
        {
            if (!_stricmp(g_ExtFontTable[i]->ASCIIFontName.c_str(), pFont->GetName()))
            {
                FontMap[pFont] = g_ExtFontTable[i];
                return g_ExtFontTable[i];
            }
        }

        return g_ExtFontTable[1];
    }

    DWORD __fastcall GetColor16(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.color[colorIdx].Value();
    }

    DWORD __fastcall GetColor32(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.palette32->colors[colorIdx];
    }

    DWORD(__fastcall* GetColor)(const H3BasePalette565& palette, int colorIdx);

    void __fastcall DrawPixcel16(const PUINT8 rowBuffer, int col, DWORD color)
    {
        *((WORD*)rowBuffer + col) = (WORD)color;
    }

    void __fastcall DrawPixcel32(const PUINT8 rowBuffer, int col, DWORD color)
    {
        *((DWORD*)rowBuffer + col) = color;
    }

    void(__fastcall* DrawPixcel)(const PUINT8 rowBuffer, int col, DWORD color);

    /**
     * @brief 绘制文字 H3中文: 0x532230 0x40C5B3
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
    bool __fastcall DrawTextChar(H3Font* pFont, ExtFont* cFont, H3LoadedPcx16* pOutputPcx, uint8_t nCode1,
                                 uint8_t nCode2, int nX, int nY, DWORD nFontColor)
    {
        // 绘制英文文字
        if (nCode2 == 0)
        {
            PUINT8 pFontBuffer = pFont->GetChar(nCode1);
            int startX = nX + pFont->width[nCode1].leftMargin;
            int startY = nY;
            for (int nRow = 0; nRow < pFont->height; ++nRow)
            {
                for (int nColumn = 0; nColumn < pFont->width[nCode1].span; ++nColumn)
                {
                    uint8_t nPixcel = *pFontBuffer++;
                    if (!nPixcel)
                    {
                        continue;
                    }

                    // 255表示绘制正常颜色，否则则绘制阴影
                    if (nPixcel == 255)
                    {
                        DrawPixcel(pOutputPcx->GetRow(startY + nRow), startX + nColumn, nFontColor);
                    }
                    else
                    {
                        DrawPixcel(pOutputPcx->GetRow(startY + nRow), startX + nColumn, ShadowColor);
                    }
                }
            }

            return false;
        }

        // 绘制汉字文字

        // 左边距为1，对齐Y中轴
        int startX = nX + cFont->MarginLeft;
        int startY = nY;
        PUINT8 pFontFileBuffer = cFont->GetHzkCharacterPcxPointer(nCode1, nCode2);
        for (int nRow = 0; nRow < cFont->Height; ++nRow)
        {
            for (int nColumn = 0; nColumn < cFont->Width; ++nColumn)
            {
                uint8_t alpha = *(pFontFileBuffer + (cFont->Height * nRow + nColumn));
                if (alpha == 0)
                {
                    continue;
                }

                auto rgbFontColor = H3ARGB888(nFontColor);
                rgbFontColor.Darken(-alpha);
                DrawPixcel(pOutputPcx->GetRow(startY + nRow), startX + nColumn, rgbFontColor.Value());
                // 是否绘制阴影
                if (!cFont->DrawShadow)
                {
                    continue;
                }
                // 绘制阴影
                DrawPixcel(pOutputPcx->GetRow(startY + nRow + 1), startX + nColumn + 1, ShadowColor);
            }
        }

        return true;
    }

    /**
     * @brief 读取字宽 H3中文: 0x403ABC 0x5331A0
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param nChar 字符代码
     * @return
     */
    int __fastcall GetFontCharWidth(H3Font* pFont, ExtFont* cFont, uint8_t nChar)
    {
        if (nChar < 160)
        {
            return pFont->width[nChar].leftMargin + pFont->width[nChar].span + pFont->width[nChar].rightMargin;
        }
        else
        {
            return cFont->MarginLeft + cFont->Width + cFont->MarginRight;
        }
    }

    /**
     * @brief 拆分行
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param pStr 文本指针
     * @param nWidth 行宽
     * @param textLines 文本行
     * @param textLines H3文本行
     * @return 总行数
     */
    int __fastcall SplitTextToLines(H3Font* pFont, ExtFont* cFont, LPCSTR pStr, int nWidth,
                                    vector<TextLineStruct>* textLines, H3Vector<H3String>* stringVector)
    {
        string_view text = pStr;
        auto sections = text | std::views::split('\n') |
                        std::views::transform([](const auto& v) { return string_view(v.begin(), v.end()); });

        int lineCount = 0;
        int currentLineWidth = 0;
        for (const auto& pLine : sections)
        {
            int strLength = pLine.length();
            if (strLength == 0)
            {
                ++lineCount;
                if (textLines)
                {
                    textLines->push_back(TextLineStruct{"", 0});
                }
                if (stringVector)
                {
                    stringVector->Add("");
                }
                continue;
            }

            int colorMark = false;
            int stringSubIndex = 0;
            for (int i = 0; i < strLength; ++i)
            {
                uint8_t currentChar = pLine[i];
                int charWidth = 0;

                if (Cmpt_TextColor)
                {
                    if (colorMark)
                    {
                        if (currentChar == '}')
                        {
                            colorMark = false;
                        }
                        continue;
                    }

                    if (currentChar == '{' && i + 1 < strLength && pLine[i + 1] == '~')
                    {
                        colorMark = true;
                        continue;
                    }
                }

                if (currentChar != '{' && currentChar != '}')
                {
                    charWidth = GetFontCharWidth(pFont, cFont, currentChar);
                }

                if (currentLineWidth + charWidth > nWidth)
                {
                    ++lineCount;
                    if (textLines)
                    {
                        textLines->push_back(TextLineStruct{pLine.substr(stringSubIndex, i - stringSubIndex),
                                                            i - stringSubIndex, currentLineWidth});
                    }
                    if (stringVector)
                    {
                        stringVector->Add(
                            H3String(pLine.substr(stringSubIndex, i - stringSubIndex).data(), i - stringSubIndex));
                    }
                    stringSubIndex = i;
                    currentLineWidth = charWidth;
                }
                else
                {
                    currentLineWidth += charWidth;
                }

                if (currentChar > 160 && (i + 1) <= strLength)
                {
                    ++i;
                }
            }

            if (strLength - stringSubIndex > 0)
            {
                ++lineCount;
                if (textLines)
                {
                    textLines->push_back(TextLineStruct{pLine.substr(stringSubIndex, strLength - stringSubIndex),
                                                        strLength - stringSubIndex, currentLineWidth});
                }
                if (stringVector)
                {
                    stringVector->Add(H3String(pLine.substr(stringSubIndex, strLength - stringSubIndex).data(),
                                               strLength - stringSubIndex));
                }
            }

            currentLineWidth = 0;
        }

        return lineCount;
    }

    /**
     * @brief 绘制文字 H3中文: 0x4077D4 0x532BC0
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
    void __stdcall TextDraw(HiHook* h, H3Font* pFont, LPCSTR pStr, H3LoadedPcx16* pPcx, int nX, int nY, int nWidth,
                            int nHeight, uint32_t nColorIdx, uint32_t nAlignFlags, int nFontStyle)
    {
        if (nWidth == 0)
        {
            return;
        }

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

        // 汉字字体
        ExtFont* cFont = GetMappedExtFont(pFont);

        vector<TextLineStruct> textLines;
        SplitTextToLines(pFont, cFont, pStr, nWidth, &textLines, nullptr);

        int startY = 0;
        // 垂直居中对齐
        if (nAlignFlags & eTextAlignment::VCENTER)
        {
            nAlignFlags &= ~eTextAlignment::VCENTER;
            int nTotalHeight = pFont->height * textLines.size(); // 文本总高度
            if (nTotalHeight >= nHeight)
            {
                if (nHeight < 2 * pFont->height)
                {
                    startY = (nHeight - pFont->height) / 2;
                }
            }
            else
            {
                startY = (nHeight - nTotalHeight) / 2;
            }
        }
        // 垂直底部对齐
        if (nAlignFlags & eTextAlignment::VBOTTOM)
        {
            nAlignFlags &= ~eTextAlignment::VBOTTOM;
            int nTotalHeight = pFont->height * textLines.size(); // 文本总高度
            if (nTotalHeight < nHeight)
            {
                startY = nHeight - nTotalHeight;
            }
        }

        int cfontShift = startY + (pFont->height - cFont->Height) / 2.0;

        // 处理颜色代码
        nColorIdx = nColorIdx & 0x100 ? nColorIdx & 0xFE : nColorIdx + 9;

        DWORD defaultColor = GetColor(pFont->palette, nColorIdx);
        DWORD textColor = defaultColor;

        int rowIdx = 0;
        for (const TextLineStruct& p : textLines)
        {
            // 水平左右对齐
            int startX = 0;
            switch (nAlignFlags)
            {
            case 0:
                startX = 0;
                break;
            case 1:
                startX = (nWidth - p.nWidth) / 2;
                break;
            case 2:
                startX = nWidth - p.nWidth;
                break;
            }

            int colorNameSubIndex = 0;
            int posMove = 0;
            for (int i = 0; i < p.nStrLength; ++i)
            {
                uint8_t currentChar = (uint8_t)p.pText[i];
                if (currentChar == 0xFF)
                {
                    continue;
                }

                // 结束文本截取
                if (colorNameSubIndex)
                {
                    if (currentChar == '}')
                    {
                        string_view colorName = p.pText.substr(colorNameSubIndex, i - colorNameSubIndex);
                        if (colorName[0] == '#' && colorName.length() <= 9)
                        {
                            auto rst = std::from_chars(colorName.data() + 1, colorName.data() + colorName.size(),
                                                       textColor, 16);
                            if (rst.ec != std::errc())
                            {
                                textColor = 0u;
                            }
                        }
                        else
                        {
                            textColor = TextColorMap[colorName].value_or(0u);
                        }
                        colorNameSubIndex = 0;
                    }
                    continue;
                }

                if (currentChar == '{') // 字符: '{'
                {
                    // 判断是否是特殊颜色代码开始
                    if (Cmpt_TextColor && currentChar == '{' && i + 1 < p.nStrLength && p.pText[i + 1] == '~')
                    {
                        colorNameSubIndex = i + 2;
                        continue;
                    }

                    // 传统颜色代码
                    if (currentChar == '{')
                    {
                        textColor = GetColor(pFont->palette, nColorIdx + 1);
                    }
                    continue;
                }
                else if (currentChar == '}') // 字符: '}'
                {
                    colorNameSubIndex = 0;
                    textColor = defaultColor;
                    continue;
                }

                if (currentChar > 160 && (i + 1) <= p.nStrLength)
                {
                    DrawTextChar(pFont, cFont, pPcx, currentChar, p.pText[i + 1], nX + startX + posMove,
                                 nY + cfontShift +
                                     rowIdx * (std::max(pFont->height, cFont->Height) + cFont->MarginBottom),
                                 textColor);
                    ++i;
                }
                else
                {
                    DrawTextChar(pFont, cFont, pPcx, currentChar, 0, nX + startX + posMove,
                                 nY + startY + rowIdx * (std::max(pFont->height, cFont->Height) + cFont->MarginBottom),
                                 textColor);
                }

                posMove += GetFontCharWidth(pFont, cFont, currentChar);
            }

            ++rowIdx;

            if (startY + (rowIdx + 1) * pFont->height > startY + nHeight)
            {
                break;
            }
        }
    }

    /**
     * @brief 拆分文本为行 H3Complete: 0x4B58F0
     * @param pFont ASCII字体
     * @param pStr 文本字符串
     * @param nWidth 文本框宽度
     * @param stringVector 拆分后的文本行容器
     * @return
     */
    void __stdcall SplitTextIntoLines(HiHook* h, H3Font* pFont, char* pStr, const int iBoxWidth,
                                      H3Vector<H3String>& stringVector)
    {
        if (iBoxWidth == 0 || !*pStr)
        {
            return;
        }

        ExtFont* cFont = GetMappedExtFont(pFont);

        //获取空格宽度
        int spaceWidth = pFont->width[32].span + pFont->width[32].leftMargin + pFont->width[32].rightMargin;

        int lineWidth = 0;
        H3String strBuffer;

        while (*pStr)
        {
            // 行首空格和换行符处理
            int blankWidth = 0;
            int blankCount = 0;
            for (UINT8 code = *pStr; code == ' ' || code == '\n'; code = *++pStr)
            {
                if (code == ' ')
                {
                    blankWidth += spaceWidth;
                    ++blankCount;
                    continue;
                }

                stringVector.Add(strBuffer);
                strBuffer = H3String();
                lineWidth = 0;
                blankWidth = 0;
                blankCount = 0;
                ++pStr; // 换行时额外移动一格光标
            }

            // 通过空格或换行符取词，一个汉字算作一个词
            int wordWidth = 0;
            char* wordCursor = pStr;
            for (UINT8 code = *wordCursor; *wordCursor != '\0'; code = *++wordCursor)
            {
                if (code == ' ' || code == '\n')
                {
                    break;
                }

                if (code > 160u && *(wordCursor + 1))
                {
                    wordWidth = cFont->GlyphWidth;
                    wordCursor += 2;
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
                    stringVector.Add(strBuffer);
                    strBuffer = H3String();
                    lineWidth = 0;
                }

                blankCount = 0; // 移除行首空格
                blankWidth = 0; // 重置空格宽度

                // 如果取词的宽超过过了文本框的宽
                // 强行断行，将词拆行显示直到词宽，
                // 小于文本边框宽度
                while (wordWidth > iBoxWidth)
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

                        strBuffer.Append(code);
                        lineWidth += charWidth;
                        wordWidth -= charWidth;
                    };

                    stringVector.Add(strBuffer);
                    lineWidth = 0;
                    strBuffer = H3String();
                }
            }

            // 插入词前空格
            strBuffer.Insert(blankCount, ' ');

            // 将词填入句末
            if (pStr != wordCursor)
            {
                strBuffer.Append(pStr, wordCursor - pStr);
                pStr = wordCursor;
            };

            // 句宽增加词的宽度
            lineWidth += wordWidth;
            // 句宽增加空格宽度
            lineWidth += blankWidth;
        }

        // 文本剩余单独成句
        if (lineWidth > 0)
        {
            stringVector.Add(strBuffer);
        }
    }

    /**
     * @brief 获取文本中最长单词
     * @param h 函数钩
     * @param pFont 字体
     * @param szText 文本段
     * @return 最长的一个词
     */
    int __stdcall GetWordWidth(HiHook* h, H3Font* pFont, char* szText)
    {
        ExtFont* cFont = GetMappedExtFont(pFont);

        int strLength = strlen(szText);
        int maxWidth = cFont->MarginLeft + cFont->Width + cFont->MarginRight;
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
            if (code < 0xA1 || code == 0xFF)
            {
                wordWidth += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                continue;
            }

            // GBK范围0xA1 - OxFE，位码0x01-0xFE
            UINT8 extCode = szText[i + 1];
            if (extCode == '\0' || extCode == 0xFF) // GBK 0xFF位码为空
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
    int __stdcall GetLineWrapWidth(HiHook* h, H3Font* pFont, char* szText, int iBoxWidth)
    {
        ExtFont* cFont = GetMappedExtFont(pFont);
        int cFontWidth = cFont->MarginLeft + cFont->Width + cFont->MarginRight;

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
            if (code < 0xA1 || code == 0xFF)
            {
                charWidth = pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                charSize = 1; // 单字节
            }
            else
            {
                // 获取位码
                UINT8 extCode = szText[i + 1];
                // GBK范围0xA1 - OxFE，位码0x01-0xFE
                if (extCode != '\0' && extCode != 0xFF) // GBK 0xFF位码为空
                {
                    charWidth = cFontWidth;
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
    int __stdcall GetLineCount(HiHook* h, H3Font* pFont, char* szText, int iBoxWidth)
    {
        ExtFont* cFont = GetMappedExtFont(pFont);
        int cFontWidth = cFont->MarginLeft + cFont->Width + cFont->MarginRight;

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
            if (code < 0xA1 || code == 0xFF)
            {
                charWidth = pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                charSize = 1; // 单字节
            }
            else
            {
                // 获取位码
                UINT8 extCode = szText[i + 1];
                // GBK范围0xA1 - OxFE，位码0x01-0xFE
                if (extCode != '\0' && extCode != 0xFF) // GBK 0xFF位码为空
                {
                    charWidth = cFontWidth;
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
    int __stdcall GetLineWidth(HiHook* h, H3Font* pFont, char* szText)
    {
        ExtFont* cFont = GetMappedExtFont(pFont);

        int strLength = strlen(szText);

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
            if (code < 0xA1 || code == 0xFF)
            {
                lineWidth += pFont->width[code].leftMargin + pFont->width[code].span + pFont->width[code].rightMargin;
                continue;
            }

            // 获取位码
            UINT8 extCode = szText[i + 1];
            // GBK范围0xA1 - OxFE，位码0x01-0xFE
            if (extCode == '\0' || extCode == 0xFF) // GBK 0xFF位码为空
            {
                continue;
            }

            lineWidth += cFont->MarginLeft + cFont->Width + cFont->MarginRight;
            ++i;
        }

        return max(lineWidth, maxWidth);
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

        _P = GetPatcher();
        _PI = _P->CreateInstance("HD.Plugin.H3FontExtension");

        // 加载配置
        try
        {
            auto config = toml::parse_file("H3CN.toml");

            toml::array fontArr = *config["Fonts"].as_array();

            for (int i = 0; i < 9; ++i)
            {
                const auto& font = fontArr[i].as_table();
                g_ExtFontTable[i] =
                    new ExtFont(font->get("Name")->value_or(""), font->get("ExtFont")->value_or(""),
                                font->get("Height")->value_or(0), font->get("Width")->value_or(0),
                                font->get("MarginLeft")->value_or(0), font->get("MarginRight")->value_or(0),
                                font->get("MarginBottom")->value_or(2), font->get("DrawShadow")->value_or(true));
            }

            Cmpt_TextColor = config["General"]["TextColor"].value_or(true);
            if (Cmpt_TextColor)
            {
                auto configColor = toml::parse_file("H3CN.TextColor.toml");
                TextColorMap = *configColor["TextColor"].as_table();
            }

            // 文本行宽计算规则限制
            MinLineWidth = config["MessageBox"]["MinLineWidth"].value_or(256);
            MaxLineWidth = config["MessageBox"]["MaxLineWidth"].value_or(-1);
            if (MaxLineWidth == -1)
            {
                MaxLineWidth = H3GameWidth::Get() / 2 - 32 * 2;
            }
        }
        catch (const std::exception&)
        {
            MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
        }

        // 注入函数劫持
        _PI->WriteHiHook(0x4B51F0, SPLICE_, THISCALL_, TextDraw);
        _PI->WriteHiHook(0x4B5580, SPLICE_, THISCALL_, GetLineCount);     // 计算文本的行数
        _PI->WriteHiHook(0x4B56F0, SPLICE_, THISCALL_, GetLineWidth);     // 最长文本行长度
        _PI->WriteHiHook(0x4B5770, SPLICE_, THISCALL_, GetWordWidth);     // 最长单词长度
        _PI->WriteHiHook(0x4B57E0, SPLICE_, THISCALL_, GetLineWrapWidth); // 最长换行长度
        _PI->WriteHiHook(0x4B58F0, SPLICE_, THISCALL_, SplitTextIntoLines);

        return true;
    }
} // namespace H3FontExtension
