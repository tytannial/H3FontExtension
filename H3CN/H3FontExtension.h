#pragma once

#include <map>
#include <ranges>
#include <vector>

#define _H3API_PATCHER_X86_

#include <H3API.hpp>
#include <toml.hpp>

static Patcher* _P;
static PatcherInstance* _PI;

namespace H3FontExtension
{
    // 双字节码表定义
    constexpr uint16_t GBK_SECTION = 129;

    // 字符阴影颜色
    constexpr uint16_t ShadowColor = 0;

    static bool Cmpt_TextColor = true;
    static toml::table TextColorMap;

    static int BoxWidthModify = 0;

    struct TextLineStruct
    {
        h3::H3String pText;
        UINT iLength;
        int iWidth;
    };

    struct ExtFont
    {
    public:
        std::string ASCIIFontName;
        PUINT8 FontFileBuffer = nullptr;
        UINT8 Height = 0;
        int Width = 0;
        int MarginLeft = 0;
        int MarginRight = 0;
        int MarginBottom = 0;
        int GlyphWidth = 0;
        bool DrawShadow = true;

        ExtFont()
        {
        }

        ExtFont(LPCSTR lpASCIIFontName, LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight,
                int iMarginBottom, bool bDrawShadow)
        {
            LoadHzhFont(lpASCIIFontName, lpFileName, iHeight, iWidth, iMarginLeft, iMarginRight, iMarginBottom,
                        bDrawShadow);
        }

        /// <summary>
        /// 汉字结构体 H3中文: 0x40CF18 0x5863B0
        /// </summary>
        /// <param name="lpFileName"></param>
        /// <param name="nHeight"></param>
        /// <param name="nWidth"></param>
        /// <returns></returns>
        bool __fastcall LoadHzhFont(LPCSTR lpASCIIFontName, LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft,
                                    int iMarginRight, int iMarginBottom, bool bDrawShadow)
        {
            std::ifstream file(lpFileName, std::ios::in | std::ios::binary);

            if (file.good() == false)
            {
                MessageBoxW(h3::H3Hwnd::Get(), L"初始化字体失败", L"错误", 0);
                return false;
            }

            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();

            this->DrawShadow = bDrawShadow;
            this->MarginRight = iMarginRight;
            this->MarginLeft = iMarginLeft;
            this->MarginBottom = iMarginBottom;
            this->Width = iWidth;
            this->Height = iHeight;
            this->ASCIIFontName = std::string(lpASCIIFontName);
            this->FontFileBuffer = new UINT8[fileSize];

            this->GlyphWidth = iMarginLeft + iWidth + iMarginRight;

            file.seekg(0, std::ios::beg);
            file.read((char*)this->FontFileBuffer, fileSize);

            return false;
        }

        /**
         * @brief 读取HZK字库字符画 H3中文: 0x4062B2 0x5325E0
         * @param section 区码
         * @param position 位码
         * @return 汉字库字符指针
         */
        inline PUINT8 __fastcall GetHzkCharacterPcxPointer(UINT8 section, UINT8 position)
        {
            return this->FontFileBuffer + this->Width * this->Height * ((section - 0x81) * 0xBF + position - 0x40);
        }
    };

    // 汉字字体全局变量
    static ExtFont* g_ExtFontTable[9];

    static std::map<h3::H3Font*, ExtFont*> FontMap;

    bool Init();
} // namespace H3FontExtension
