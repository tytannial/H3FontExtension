#pragma once

#include <unordered_map>

#include <toml.hpp>

#define _H3API_PATCHER_X86_
#include <H3API.hpp>

static Patcher* _P;
static PatcherInstance* _PI;

namespace H3FontExtension
{
	/* 双字节码表定义
	 * GBK范围
	 * 0x8140-A0FE，收录 CJK 汉字 6080 个
	 * 0xA1A1-A9FE，除 GB 2312 的符号外，还增补了其它符号
	 * 0xAA40-A0FE，收录 CJK 汉字和增补的汉字 8160 个
	 * 0xA840-A9A0，扩除非汉字区
	 * 0xB0A1-F7FE，收录 GB 2312 汉字 6763 个，按原序排列
	 * BIG5范围
	 * 0xA140-0xA3BF：标点符号、希腊字母及特殊符号
	 * 0xA440-0xC67E：常用汉字，先按笔划再按部首排序
	 */

	 // 双字节区码
	constexpr uint8_t DBCS_SECTION = 0x81;
	// 双字节位码
	constexpr uint8_t DBCS_POSITION = 0x40;

	// 字符阴影颜色
	constexpr uint16_t ShadowColor = 0;

	static bool IsTextColorEnable = true;
	static toml::table TextColorMap;

	static int BoxWidthMin = 0;
	static int BoxWidthMax = 0;

	struct TextLineStruct
	{
		std::string Text;
		int lineWidth;
	};

	struct ExtFont
	{
	public:
		PUINT8 FontFileBuffer = nullptr;
		INT8 Height = 0;
		int Width = 0;
		int MarginLeft = 0;
		int MarginRight = 0;
		int MarginBottom = 0;
		int GlyphWidth = 0;
		bool DrawShadow = true;
		int LineHeightAdjust = 0;

		ExtFont()
		{
		}

		ExtFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight, int iMarginBottom,
			bool bDrawShadow, int lineHeightAdjust)
		{
			LoadHzhFont(lpFileName, iHeight, iWidth, iMarginLeft, iMarginRight, iMarginBottom, bDrawShadow, lineHeightAdjust);
		}

		/// <summary>
		/// 扩展字库结构体
		/// </summary>
		/// <param name="lpFileName"></param>
		/// <param name="nHeight"></param>
		/// <param name="nWidth"></param>
		/// <returns></returns>
		bool __fastcall LoadHzhFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight,
			int iMarginBottom, bool bDrawShadow, int lineHeightAdjust)
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
			this->LineHeightAdjust = lineHeightAdjust;

			file.seekg(0, std::ios::end);
			std::streampos fileSize = file.tellg();
			this->FontFileBuffer = new UINT8[fileSize];
			file.seekg(0, std::ios::beg);
			file.read((char*)this->FontFileBuffer, fileSize);

			return false;
		}

		/**
		 * @brief 字体字符串指针
		 * @param section 区码
		 * @param position 位码
		 * @return 扩展字库字符图像指针
		 */
		inline PUINT8 __fastcall GetExtGlyphDataPtr(UINT8 section, UINT8 position) const
		{
			return this->FontFileBuffer +
				this->Width * this->Height * ((section - DBCS_SECTION) * 0xBF + position - DBCS_POSITION);
		}
	};

	struct H3FontExt : h3::H3Font
	{
		int oriHeight;
		ExtFont* ExtData;
	};

	// 游戏内字体映射
	static std::unordered_map<std::string, ExtFont> g_ExtFontTable;

	bool Init();
} // namespace H3FontExtension
