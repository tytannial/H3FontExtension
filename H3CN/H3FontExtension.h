#pragma once

#include <map>
#include <ranges>
#include <vector>

#include <H3API.hpp>
#include <toml.hpp>

namespace H3FontExtension
{
    const uint32_t ShadowColor32 = 0xFF000000;
    const uint16_t ShadowColor16 = 0x0000;

    static bool Cmpt_TextColor = true;
    static toml::table TextColorMap;

    static int MaxWordWidth = 400;
    static int MaxLineWidth = 400;

    struct TextLineStruct
    {
        std::string_view pText;
        int nStrLength;
        int nWidth;
    };

    struct HzkStrc
    {
    public:
        std::string ASCIIFontName;
        PUINT8 FontFileBuffer = nullptr;
        UINT8 Height = 0;
        int Width = 0;
        int MarginLeft = 0;
        int MarginRight = 0;
        int MarginBottom = 0;
        bool DrawShadow = true;

        HzkStrc()
        {
        }

        HzkStrc(LPCSTR lpASCIIFontName, LPCSTR lpFileName, int nHeight, int nWidth, int nMarginLeft, int nMarginRight,
                int nMarginBottom, bool bDrawShadow)
        {
            LoadHzhFont(lpASCIIFontName, lpFileName, nHeight, nWidth, nMarginLeft, nMarginRight, nMarginBottom,
                        bDrawShadow);
        }

        /// <summary>
        /// 汉字结构体 H3中文: 0x40CF18 0x5863B0
        /// </summary>
        /// <param name="lpFileName"></param>
        /// <param name="nHeight"></param>
        /// <param name="nWidth"></param>
        /// <returns></returns>
        bool __fastcall LoadHzhFont(LPCSTR lpASCIIFontName, LPCSTR lpFileName, int nHeight, int nWidth, int nMarginLeft,
                                    int nMarginRight, int nMarginBottom, bool bDrawShadow)
        {
            std::ifstream file(lpFileName, std::ios::in | std::ios::binary);
            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();

            if (fileSize == 0)
            {
                MessageBoxA(h3::H3Hwnd::Get(), "Init Font Error", "HeroesIII", 0);
                return false;
            }

            this->DrawShadow = bDrawShadow;
            this->MarginRight = nMarginRight;
            this->MarginLeft = nMarginLeft;
            this->MarginBottom = nMarginBottom;
            this->Width = nWidth;
            this->Height = nHeight;
            this->ASCIIFontName = std::string(lpASCIIFontName);
            this->FontFileBuffer = new uint8_t[fileSize];

            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<std::ifstream::char_type*>(this->FontFileBuffer), fileSize);

            return false;
        }

        /**
         * @brief 读取HZK字库字符画 H3中文: 0x4062B2 0x5325E0
         * @param section 区码
         * @param position 位码
         * @return 汉字库字符指针
         */
        inline PUINT8 __fastcall GetHzkCharacterPcxPointer(uint8_t section, uint8_t position)
        {
            // GB2312
            // return this->FontFileBuffer + this->Width * ((this->Height + 7) >> 3) * (0x5E * (section - 0xA1) +
            // position - 0xA1);

            // GBK
            return this->FontFileBuffer +
                   this->Width * ((this->Height + 7) >> 3) * ((section - 0x81) * 0xBF + position - 0x40);
        }
    };

    //汉字字体全局变量
    static HzkStrc* HzkFont[9];

    static std::map<h3::H3Font*, HzkStrc*> FontMap;

    typedef bool(__fastcall* DrawTextChar)(h3::H3Font* pFont, HzkStrc* cFont, h3::H3LoadedPcx16* pOutputPcx,
                                           uint8_t nCode1, uint8_t nCode2, int nX, int nY, uint32_t nTextColor);

    bool Init();
} // namespace H3FontExtension
