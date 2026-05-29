#include "H3FontExtension.h"

using namespace h3;
using namespace std;

namespace H3FontExtension
{
	/**
	 * @brief 计算单个字符的宽度
	 */
	inline static int GetH3CharWidth(const H3Font::FontSpacing* widthArr, uint8_t code)
	{
		return widthArr[code].leftMargin + widthArr[code].span + widthArr[code].rightMargin;
	}

	/**
	 * @brief 判断是否为双字节字符（GBK/DBCS 编码）
	 */
	inline static bool IsDBCSLeadByte(uint8_t code, uint8_t nextCode)
	{
		return code >= DBCS_SECTION && nextCode && nextCode != 0xFF && nextCode >= DBCS_POSITION;
	}

	constexpr inline static WORD RGB888toRGB565(DWORD color)
	{
		return ((((color >> 16) & 0xFF) >> 3) & 0x1F) << 11 | ((((color >> 8) & 0xFF) >> 2) & 0x3F) << 5 |
			(((color & 0xFF) >> 3) & 0x1F);
	}

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
	 * @brief 将一行原始文本（含颜色码）预处理为 CleanLine
	 * @param src            原始拆行结果
	 * @param defaultColor   默认渲染颜色
	 * @param highlightColor { } 的高亮颜色
	 * @param is32bit        当前是否 32 位色深
	 * @param dst            输出
	 */
	static void PreprocessLine(const TextLineStruct& src, DWORD  defaultColor, DWORD  highlightColor, bool is32bit, CleanLine& dst)
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
							// 注意：原代码中 # 格式不做 RGB888→RGB565 转换
							DWORD c = 0;
							auto [_, ec] = std::from_chars(
								raw.data() + codeStart + 1,
								raw.data() + i,
								c, 16);
							newColor = (ec == std::errc()) ? c : defaultColor;
						}
						else
						{
							// 命名颜色：从配置表查询
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
			if (code < DBCS_SECTION || code == 0xFF)
			{
				dst.text.push_back(static_cast<char>(code));
				++i;
			}
			else
			{
				// 双字节字符，拷贝两个字节
				dst.text.push_back(raw[i]);
				if (i + 1 < len)
				{
					dst.text.push_back(raw[i + 1]);
					i += 2;
				}
				else
				{
					++i; // 不完整的双字节尾部，跳过
				}
			}
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
	static void __stdcall SplitTextIntoLines(H3FontExt* pFont, char* szText, const int iBoxWidth,
		vector<TextLineStruct>& lines)
	{
		if (!*szText)
			return;

		// 缓存常用参数，避免循环中反复解引用
		const auto* widthArr = pFont->width;
		const int spaceWidth = GetH3CharWidth(widthArr, 32);
		const int glyphWidth = pFont->ExtData->GlyphWidth;
		const bool colorEnabled = IsTextColorEnable;

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

			// ---- 阶段 2：取词——确定词边界并预计算词宽（不含字符拷贝）----
			int wordWidth = 0;
			char* wordStart = szText;
			char* wordEnd = szText;

			while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n')
			{
				uint8_t code = static_cast<uint8_t>(*wordEnd);

				// 跳过颜色码标记 {~...} 和 { }
				if (colorEnabled && (code == '{' || code == '}'))
				{
					if (code == '{' && *(wordEnd + 1) == '~')
					{
						// 跳过整个 {~RRGGBB}
						wordEnd += 2; // 跳过 {~
						while (*wordEnd && *wordEnd != '}' && *wordEnd != ' ' && *wordEnd != '\n')
							++wordEnd;
						if (*wordEnd == '}')
							++wordEnd;
						continue;
					}
					else
					{
						++wordEnd; // 单独的 { 或 }
						continue;
					}
				}

				// 双字节字符
				uint8_t nextCode = static_cast<uint8_t>(*(wordEnd + 1));
				if (IsDBCSLeadByte(code, nextCode))
				{
					wordWidth += glyphWidth;
					wordEnd += 2;
					continue;
				}

				// 单字节字符
				wordWidth += GetH3CharWidth(widthArr, code);
				++wordEnd;
			}

			int wordLen = static_cast<int>(wordEnd - wordStart);

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
					char* p = wordStart;
					while (p < wordEnd)
					{
						uint8_t code = static_cast<uint8_t>(*p);
						int charW = 0;
						int charBytes = 1;

						// 跳过颜色码
						if (colorEnabled && (code == '{' || code == '}'))
						{
							if (code == '{' && *(p + 1) == '~')
							{
								p += 2;
								while (p < wordEnd && *p != '}')
									++p;
								if (p < wordEnd)
									++p;
								continue;
							}
							++p;
							continue;
						}

						uint8_t nextCode = static_cast<uint8_t>(*(p + 1));
						if (IsDBCSLeadByte(code, nextCode))
						{
							charW = glyphWidth;
							charBytes = 2;
						}
						else
						{
							charW = GetH3CharWidth(widthArr, code);
						}

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
					continue; // 词已处理完毕，跳过阶段4
				}
			}

			// ---- 阶段 4：将词追加到当前行 ----
			if (blankCount > 0)
				lineBuf.append(blankCount, ' ');

			lineBuf.append(wordStart, wordLen);
			lineWidth += wordWidth + blankWidth;
			szText = wordEnd;
		}

		// 输出最后一行
		if (lineWidth > 0 || !lineBuf.empty())
		{
			lines.push_back({ std::move(lineBuf), lineWidth });
		}
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
			for (int rowIdx = 0; rowIdx < pFont->oriHeight; ++rowIdx)
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
		PUINT8 pFontFileBuffer = cFont->GetExtGlyphDataPtr(cHiCode, cLoCode);

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
		const auto* w = pFont->width;

		const int ascShift = (fontHeight - oriHeight) / 2;
		const int extShift = (fontHeight - extHeight) / 2;

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

		// ========== 阶段六：绘制 ==========
		const int bottomBound = iY + iBoxHeight;

		for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
		{
			const CleanLine& line = cleanLines[rowIdx];
			if (line.lineWidth == 0)
				continue;

			// 预计算当前行的 Y 坐标（按行算一次，而非每字符算）
			const int lineY = iY + startY + rowIdx * fontHeight;
			if (lineY + fontHeight > bottomBound)
				break;

			// 水平对齐
			int startX = 0;
			switch (uAlignFlags)
			{
			case 1: startX = (iBoxWidth - line.lineWidth) / 2; break;
			case 2: startX = iBoxWidth - line.lineWidth;         break;
			}

			// ---- 绘制内层循环 ----
			int posMove = 0;
			const char* p = line.text.data();
			const char* end = p + line.text.size();

			// 快速路径：无颜色变更，全行使用默认颜色
			if (line.stops.empty())
			{
				while (p < end)
				{
					const uint8_t code = static_cast<uint8_t>(*p);

					if (code < DBCS_SECTION || code == 0xFF)
					{
						H3Font_DrawChar(pFont, pPcx, code, 0,
							iX + startX + posMove, lineY + ascShift, defaultColor);
						posMove += w[code].leftMargin + w[code].span + w[code].rightMargin;
						++p;
					}
					else
					{
						H3Font_DrawChar(pFont, pPcx, code, static_cast<uint8_t>(p[1]),
							iX + startX + posMove, lineY + extShift, defaultColor);
						posMove += glyphWidth;
						p += 2;
					}
				}
			}
			else
			{
				// 通用路径：跟踪颜色变更点
				DWORD curColor = defaultColor;
				size_t stopIdx = 0;
				const size_t stopCount = line.stops.size();

				while (p < end)
				{
					// 检查当前位置是否有颜色变更
					const size_t bytePos = static_cast<size_t>(p - line.text.data());
					if (stopIdx < stopCount && line.stops[stopIdx].bytePos == bytePos)
					{
						curColor = line.stops[stopIdx].color;
						++stopIdx;
					}

					const uint8_t code = static_cast<uint8_t>(*p);

					if (code < DBCS_SECTION || code == 0xFF)
					{
						H3Font_DrawChar(pFont, pPcx, code, 0,
							iX + startX + posMove, lineY + ascShift, curColor);
						posMove += w[code].leftMargin + w[code].span + w[code].rightMargin;
						++p;
					}
					else
					{
						H3Font_DrawChar(pFont, pPcx, code, static_cast<uint8_t>(p[1]),
							iX + startX + posMove, lineY + extShift, curColor);
						posMove += glyphWidth;
						p += 2;
					}
				}
			}
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
		lines.RemoveAll();

		if (!strlen(szText))
		{
			return;
		}

		vector<TextLineStruct> vlines;
		SplitTextIntoLines(pFont, szText, iBoxWidth, vlines);
		for (const auto& line : vlines)
		{
			lines.Add(H3String(line.Text.c_str(), line.Text.length()));
		}
	}

	/**
	 * @brief 遍历文本中所有"可见 token"，自动跳过颜色码 {~RRGGBB} / { } / }
	 *
	 * @param pFont    字体指针
	 * @param szText   输入文本
	 * @param fn       回调：fn(TokenType type, int width, int charBytes)
	 *                 - type:       token 类型
	 *                 - width:      该 token 的像素宽度（Newline 为 0）
	 *                 - charBytes:  该 token 在原始字符串中占的字节数
	 */
	template<typename Func>
	static void ForEachVisibleChar(H3FontExt* pFont, const char* szText, Func&& fn)
	{
		const auto* w = pFont->width;
		const int gw = pFont->ExtData->GlyphWidth;   // 双字节字符宽度
		const int sw = w[32].leftMargin + w[32].span + w[32].rightMargin; // 空格宽度
		const bool ce = IsTextColorEnable;
		const char* p = szText;

		while (*p)
		{
			uint8_t c = static_cast<uint8_t>(*p);

			// -------- 跳过颜色码 --------
			if (ce && (c == '{' || c == '}'))
			{
				if (c == '{' && *(p + 1) == '~')
				{
					p += 2;                                    // 跳过 "{~"
					while (*p && *p != '}' && *p != ' ' && *p != '\n')
						++p;
					if (*p == '}') ++p;                        // 跳过 '}'
					continue;
				}
				++p;                                           // 单独的 '{' 或 '}'
				continue;
			}

			// -------- 换行 --------
			if (c == '\n')
			{
				fn(TokenType::Newline, 0, 1);
				++p;
				continue;
			}

			// -------- 空格 --------
			if (c == ' ')
			{
				fn(TokenType::Space, sw, 1);
				++p;
				continue;
			}

			// -------- 单字节字符 --------
			if (c < DBCS_SECTION || c == 0xFF)
			{
				fn(TokenType::SingleByte, w[c].leftMargin + w[c].span + w[c].rightMargin, 1);
				++p;
				continue;
			}

			// -------- 双字节字符 --------
			uint8_t nc = static_cast<uint8_t>(*(p + 1));
			if (IsDBCSLeadByte(c, nc))
			{
				fn(TokenType::DoubleByte, gw, 2);
				p += 2;
			}
			else
			{
				++p;  // 无效的双字节首字节，跳过
			}
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
		if (!szText || !*szText) return 0;

		int maxWidth = 0;
		int wordWidth = 0;

		ForEachVisibleChar(pFont, szText,
			[&](TokenType type, int width, int /*bytes*/)
			{
				if (type == TokenType::Newline || type == TokenType::Space)
				{
					if (wordWidth > maxWidth) maxWidth = wordWidth;
					wordWidth = 0;
				}
				else
				{
					wordWidth += width;
				}
			});

		if (wordWidth > maxWidth) maxWidth = wordWidth;
		return clamp(maxWidth, BoxWidthMin, BoxWidthMax);
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
		if (!szText || !*szText) return 0;

		int maxWidth = 0;
		int lineWidth = 0;

		ForEachVisibleChar(pFont, szText,
			[&](TokenType type, int width, int /*bytes*/)
			{
				if (type == TokenType::Newline)
				{
					if (lineWidth > maxWidth) maxWidth = lineWidth;
					lineWidth = 0;
				}
				else
				{
					if (lineWidth + width > iBoxWidth)
					{
						if (lineWidth > maxWidth) maxWidth = lineWidth;
						lineWidth = 0;
					}
					lineWidth += width;
				}
			});

		if (lineWidth > maxWidth) maxWidth = lineWidth;
		return maxWidth;
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
		if (!szText || !*szText) return 0;

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
						lineWidth = 0;   // 当前字符归入新行
					}
					lineWidth += width;
				}
			});

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
		if (!szText || !*szText) return 0;

		int maxWidth = 0;
		int lineWidth = 0;

		ForEachVisibleChar(pFont, szText,
			[&](TokenType type, int width, int /*bytes*/)
			{
				if (type == TokenType::Newline)
				{
					if (lineWidth > maxWidth) maxWidth = lineWidth;
					lineWidth = 0;
				}
				else
				{
					lineWidth += width;
				}
			});

		if (lineWidth > maxWidth) maxWidth = lineWidth;
		return maxWidth;
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
		font->oriHeight = font->height;
		font->height = std::max(font->height, font->ExtData->Height) /*+ font->ExtData->LineHeightAdjust*/;
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

		auto maxWidth = H3GameWidth::Get() - 64 * 2;
		BoxWidthMax < 0 ? BoxWidthMax = maxWidth : BoxWidthMax = clamp(BoxWidthMax, 256, maxWidth);
		BoxWidthMin < 0 ? BoxWidthMin = maxWidth : BoxWidthMin = clamp(BoxWidthMin, 256, BoxWidthMax);
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
		_PI = _P->GetInstance("HD.Plugin.H3FontExtension");
		if (!_PI)
		{
			_PI = _P->CreateInstance("HD.Plugin.H3FontExtension");
		}
		else
		{
			_PI->UndoAll();
		}

		// 加载配置
		try
		{
			auto config = toml::parse_file("H3CN.toml");

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

			IsTextColorEnable = config["General"]["TextColor"].value_or(true);
			if (IsTextColorEnable)
			{
				TextColorMap = *config["TextColor"].as_table();
			}

			// 文本行宽计算规则限制
			BoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
			BoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
		}
		catch (const std::exception&)
		{
			MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
		}

		// 注入函数劫持

		// 挂载一个扩展函数在游戏DDraw初始化的位置 用于检测色彩模式
		_PI->WriteHiHook(0x601AB0, SPLICE_, FASTCALL_, EXTENDED_, Main_DirectDrawInit_Hook);

		// 扩展字体申请的堆大小，额外增加4bytes空间
		_PI->WriteDword(0x55B9CE + 1, sizeof(H3FontExt));

		// 字体加载后填入拓展字符区
		_PI->WriteHiHook(0x55BAE0, SPLICE_, THISCALL_, EXTENDED_, H3Font_Load_Hook);

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
