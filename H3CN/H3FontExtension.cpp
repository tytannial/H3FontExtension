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
    HzkStrc* __fastcall GetMappedHzkFont(H3Font* pFont)
    {
        auto bHzkFontExisted = FontMap.contains(pFont);
        if (bHzkFontExisted)
        {
            return FontMap[pFont];
        }

        for (size_t i = 0; i < 9; i++)
        {
            if (!_stricmp(HzkFont[i]->ASCIIFontName.c_str(), pFont->GetName()))
            {
                FontMap[pFont] = HzkFont[i];
                return HzkFont[i];
            }
        }

        return HzkFont[1];
    }

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
    template<typename T>
        requires(std::same_as<T, uint32_t> || std::same_as<T, uint16_t>)
    bool __fastcall DrawTextChar_t(H3Font* pFont, HzkStrc* cFont, H3LoadedPcx16* pOutputPcx, uint8_t nCode1,
                                   uint8_t nCode2, int nX, int nY, T nFontColor, T nShadowColor)
    {
        //绘制英文文字
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
                        *((T*)pOutputPcx->GetRow(startY + nRow) + startX + nColumn) = nFontColor;
                    }
                    else
                    {
                        *((T*)pOutputPcx->GetRow(startY + nRow) + startX + nColumn) = nShadowColor;
                    }
                }
            }

            return false;
        }

        //绘制汉字文字

        //左边距为1，对齐Y中轴
        int startX = nX + cFont->MarginLeft;
        int startY = nY + (pFont->height - cFont->Height) / 2;
        PUINT8 pFontFileBuffer = cFont->GetHzkCharacterPcxPointer(nCode1, nCode2);
        for (int nRow = 0; nRow < cFont->Height; ++nRow)
        {
            for (int nColumn = 0; nColumn < cFont->Width; ++nColumn)
            {
                uint8_t mask = *(pFontFileBuffer + nColumn / 8 + (cFont->Height + 7) / 8 * nRow);
                if ((1 << (7 - nColumn % 8)) & mask)
                {
                    //绘制正常像素点
                    *((T*)pOutputPcx->GetRow(startY + nRow) + startX + nColumn) = nFontColor;
                    //是否绘制阴影
                    if (!cFont->DrawShadow)
                    {
                        continue;
                    }
                    //判断右下角像素颜色，绘制阴影
                    auto c = pOutputPcx->GetPixel(startX + nColumn + 1, startY + nRow + 1).GetColor();
                    if (c != nFontColor)
                    {
                        *((T*)pOutputPcx->GetRow(startY + nRow + 1) + startX + nColumn + 1) = nShadowColor;
                    }
                }
            }
        }

        return true;
    }
    /**
     * @brief 32位色绘制文本
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param pOutputPcx 图像输出
     * @param nCode1 字符编码高位
     * @param nCode2 字符编码低位
     * @param nX 绘制位置左上角X坐标
     * @param nY 绘制位置左上角Y坐标
     * @param nFontColor RGB颜色码（32位色）
     * @param nShadowColor 阴影RGB颜色码（32位色）
     * @return
     */
    bool __fastcall DrawTextChar32(H3Font* pFont, HzkStrc* cFont, H3LoadedPcx16* pOutputPcx, uint8_t nCode1,
                                   uint8_t nCode2, int nX, int nY, uint32_t nFontColor)
    {
        return DrawTextChar_t(pFont, cFont, pOutputPcx, nCode1, nCode2, nX, nY, nFontColor, ShadowColor32);
    }
    /**
     * @brief 16位色绘制文本
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param pOutputPcx 图像输出
     * @param nCode1 字符编码高位
     * @param nCode2 字符编码低位
     * @param nX 绘制位置左上角X坐标
     * @param nY 绘制位置左上角Y坐标
     * @param nFontColor RGB颜色码（32位色）
     * @param nShadowColor 阴影RGB颜色码（16位色）
     * @return
     */
    bool __fastcall DrawTextChar16(H3Font* pFont, HzkStrc* cFont, H3LoadedPcx16* pOutputPcx, uint8_t nCode1,
                                   uint8_t nCode2, int nX, int nY, uint32_t nFontColor)
    {
        return DrawTextChar_t(pFont, cFont, pOutputPcx, nCode1, nCode2, nX, nY, (uint16_t)nFontColor, ShadowColor16);
    }

    /**
     * @brief 读取字宽 H3中文: 0x403ABC 0x5331A0
     * @param pFont ASCII字体
     * @param cFont 扩展字体
     * @param nChar 字符代码
     * @return
     */
    int __fastcall GetFontCharWidth(H3Font* pFont, HzkStrc* cFont, uint8_t nChar)
    {
        if (nChar < 160)
        {
            auto CharWidth = pFont->width[nChar];
            return CharWidth.leftMargin + CharWidth.span + CharWidth.rightMargin;
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
    int __fastcall GetSplitTextLinesInfo(H3Font* pFont, HzkStrc* cFont, LPCSTR pStr, int nWidth,
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

                if (currentChar > 160)
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
     * @param EDX 寄存器参数占位
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
    void __fastcall TextDraw(H3Font* pFont, uint32_t EDX, LPCSTR pStr, H3LoadedPcx16* pPcx, int nX, int nY, int nWidth,
                             int nHeight, uint32_t nColorIdx, uint32_t nAlignFlags, int nFontStyle)
    {
        if (nWidth == 0)
        {
            return;
        }

        //汉字字体
        HzkStrc* cFont = GetMappedHzkFont(pFont);

        vector<TextLineStruct> textLines;
        GetSplitTextLinesInfo(pFont, cFont, pStr, nWidth, &textLines, nullptr);

        int startY = 0;
        //垂直居中对齐
        if (nAlignFlags & eTextAlignment::VCENTER)
        {
            nAlignFlags &= ~eTextAlignment::VCENTER;
            int nTotalHeight = pFont->height * textLines.size(); //文本总高度
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
        //垂直底部对齐
        if (nAlignFlags & eTextAlignment::VBOTTOM)
        {
            nAlignFlags &= ~eTextAlignment::VBOTTOM;
            int nTotalHeight = pFont->height * textLines.size(); //文本总高度
            if (nTotalHeight < nHeight)
            {
                startY = nHeight - nTotalHeight;
            }
        }

        //处理颜色代码
        nColorIdx = nColorIdx & 0x100 ? nColorIdx & 0xFE : nColorIdx + 9;
        uint32_t defaultColor = 0u;

        DrawTextChar drawTextFunc;
        if (H3BitMode::Get() == 4)
        {
            defaultColor = pFont->palette.palette32->colors[nColorIdx].GetColor();
            drawTextFunc = DrawTextChar32;
        }
        else
        {
            defaultColor = pFont->palette.color[nColorIdx].Value();
            drawTextFunc = DrawTextChar16;
        }

        DWORD textColor = defaultColor;

        int rowIdx = 0;
        for (const TextLineStruct& p : textLines)
        {
            //水平左右对齐
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

                //结束文本截取
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

                        if (H3BitMode::Get() != 4)
                        {
                            textColor = H3RGB565(textColor).Value();
                        }

                        colorNameSubIndex = 0;
                    }
                    continue;
                }

                if (currentChar == '{') // 字符: '{'
                {
                    //判断是否是特殊颜色代码开始
                    if (Cmpt_TextColor && currentChar == '{' && i + 1 < p.nStrLength && p.pText[i + 1] == '~')
                    {
                        colorNameSubIndex = i + 2;
                        continue;
                    }

                    //传统颜色代码
                    if (currentChar == '{')
                    {
                        textColor = H3BitMode::Get() == 4 ? pFont->palette.palette32->colors[nColorIdx + 1].GetColor()
                                                          : pFont->palette.color[nColorIdx + 1].Value();
                    }
                    continue;
                }
                else if (currentChar == '}') // 字符: '}'
                {
                    colorNameSubIndex = 0;
                    textColor = defaultColor;
                    continue;
                }

                if (currentChar < 160)
                {
                    drawTextFunc(pFont, cFont, pPcx, currentChar, 0, nX + startX + posMove,
                                 nY + startY + rowIdx * pFont->height + pFont->bottomMargin, textColor);
                }
                else
                {
                    drawTextFunc(pFont, cFont, pPcx, currentChar, p.pText[i + 1], nX + startX + posMove,
                                 nY + startY + rowIdx * pFont->height + pFont->bottomMargin, textColor);
                    ++i;
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
     * @brief 计算文本行数 H3Complete: 0x4B5580
     * @param pFont ASCII字体
     * @param EDX 寄存器参数占位
     * @param pStr 文本字符串
     * @param nWidth 文本框宽度
     * @return
     */
    int __fastcall GetLinesCountInText(H3Font* pFont, uint32_t EDX, LPCSTR pStr, int nWidth)
    {
        if (nWidth == 0)
        {
            return 0;
        }

        //汉字字体
        HzkStrc* cFont = GetMappedHzkFont(pFont);
        int lineCount = GetSplitTextLinesInfo(pFont, cFont, pStr, nWidth, nullptr, nullptr);
        return lineCount;
    }

    /**
     * @brief 按照拆分字符拆分文本获取最大宽度
     * @param pFont ASCII字体
     * @param pStr 文本字符串
     * @param pSpliters 分隔符数组
     * @return
     */
    int __fastcall GetSplitWidth(H3Font* pFont, PUINT8 pStr, LPCSTR pSpliters)
    {
        //汉字字体
        HzkStrc* cFont = GetMappedHzkFont(pFont);

        //行首换行符
        while (*pStr == '\n')
            ++pStr;

        bool ignoreWidth = false;
        int maxLineWidth = 0;
        int curLineWidth = 0;
        for (uint8_t code = *pStr; code; code = *++pStr)
        {
            if (strrchr(pSpliters, code)) //换行符强制重置行宽
            {
                if (curLineWidth > maxLineWidth)
                {
                    maxLineWidth = curLineWidth;
                }
                curLineWidth = 0;
                continue;
            }

            if (code == '}')
            {
                ignoreWidth = false;
                continue;
            }
            if (ignoreWidth)
            {
                continue;
            }
            if (code == '{' && *(pStr + 1) == '~')
            {
                ignoreWidth = true;
                continue;
            }
            if (code == '{')
            {
                continue;
            }

            curLineWidth += GetFontCharWidth(pFont, cFont, *pStr);
            if (code > 160) //汉字占用双字节
            {
                ++curLineWidth;
                ++pStr;
            }
        }

        if (curLineWidth > maxLineWidth)
        {
            maxLineWidth = curLineWidth;
        }

        return min(maxLineWidth, MaxLineWidth);
    }

    /**
     * @brief 获取文本行最大宽度 H3Complete: 0x4B56F0
     * @param pFont ASCII字体
     * @param EDX 寄存器参数占位
     * @param pStr 文本字符串
     * @return
     */
    int __fastcall GetMaxLineWidth(H3Font* pFont, uint32_t EDX, PUINT8 pStr)
    {
        const char spliter[] = {'\n', '\0'};
        return GetSplitWidth(pFont, pStr, spliter);
    }

    /**
     * @brief 获取文本单词最大宽度 H3Complete: 0x4B5770
     * @param pFont ASCII字体
     * @param EDX 寄存器参数占位
     * @param pStr 文本字符串
     * @return
     */
    int __fastcall GetMaxWordWidth(H3Font* pFont, uint32_t EDX, PUINT8 pStr)
    {
        const char spliter[] = {'\n', ' ', '\0'};
        return GetSplitWidth(pFont, pStr, spliter);
    }

    /**
     * @brief 拆分文本为行 H3Complete: 0x4B58F0
     * @param pFont ASCII字体
     * @param EDX 寄存器参数占位
     * @param pStr 文本字符串
     * @param nWidth 文本框宽度
     * @param stringVector 拆分后的文本行容器
     * @return
     */
    void __fastcall SplitTextIntoLines(H3Font* pFont, uint32_t EDX, LPCSTR pStr, int nWidth,
                                       H3Vector<H3String>& stringVector)
    {
        if (nWidth == 0)
        {
            return;
        }

        /*
         * TODO:
         * 颜色标记在前一行此时跨行，那么需要添加颜色标记在行首。
         * 这个问题仅出现在使用H3容器提前拆分行的情况，需要特殊处理。
         */

        //汉字字体
        HzkStrc* cFont = GetMappedHzkFont(pFont);
        GetSplitTextLinesInfo(pFont, cFont, pStr, nWidth, nullptr, &stringVector);
    }

    /**
     * @brief 插件配置初始化
     * @return 初始化状态
     */
    bool Init()
    {
        //加载配置
        auto config = toml::parse_file("H3CN.toml");

        toml::array fontArr = *config["Fonts"].as_array();

        for (int i = 0; i < 9; ++i)
        {
            const auto& font = fontArr[i].as_table();
            HzkFont[i] = new HzkStrc(font->get("Name")->value_or(""), font->get("HzkFont")->value_or(""),
                                     font->get("Height")->value_or(0), font->get("Width")->value_or(0),
                                     font->get("MarginLeft")->value_or(0), font->get("MarginRight")->value_or(0),
                                     font->get("DrawShadow")->value_or(true));
        }

        Cmpt_TextColor = config["General"]["TextColor"].value_or(true);
        if (Cmpt_TextColor)
        {
            auto configColor = toml::parse_file("H3CN.TextColor.toml");
            TextColorMap = *configColor["TextColor"].as_table();
        }

        // 文本框配置
        MaxWordWidth = config["MessageBox"]["MaxWordWidth"].value_or(-1);
        if (MaxWordWidth == -1)
        {
            MaxWordWidth = H3GameWidth::Get() / 2 - 32 * 2;
        }
        MaxLineWidth = config["MessageBox"]["MaxLineWidth"].value_or(-1);
        if (MaxLineWidth == -1)
        {
            MaxLineWidth = H3GameWidth::Get() / 2 - 32 * 2;
        }

        //注入函数劫持
        H3Patcher::NakedHook5(0x4B51F0, (H3NakedFunction)TextDraw);
        H3Patcher::NakedHook5(0x4B5580, (H3NakedFunction)GetLinesCountInText);
        H3Patcher::NakedHook5(0x4B56F0, (H3NakedFunction)GetMaxLineWidth);
        H3Patcher::NakedHook5(0x4B5770, (H3NakedFunction)GetMaxWordWidth);
        H3Patcher::NakedHook5(0x4B58F0, (H3NakedFunction)SplitTextIntoLines);

        return true;
    }
} // namespace H3FontExtension
