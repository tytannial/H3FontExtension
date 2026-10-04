//===----------------------------------------------------------------------===//
// ERA EML 内嵌 DEF 图片：资源缓存、帧尺寸与裁剪绘制实现
//===----------------------------------------------------------------------===//

#include "DefImage.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

namespace H3FontExtension
{
    // ============================================================
    // 基础路径与 PNG 覆盖帧尺寸
    // ============================================================

    static wchar_t gBaseDir[MAX_PATH] = {};

    void setSpriteBaseDir(const wchar_t* dir)
    {
        if (!dir)
            return;

        wcsncpy_s(gBaseDir, dir, _TRUNCATE);

        // 去掉尾部路径分隔符（组合路径时统一添加）
        std::size_t len = wcslen(gBaseDir);
        while (len > 0 && (gBaseDir[len - 1] == L'\\' || gBaseDir[len - 1] == L'/'))
            gBaseDir[--len] = L'\0';
    }

    /** @brief 读取 PNG 文件头 IHDR 宽高（仅尺寸，不做像素解码） */
    static bool readPngSize(const wchar_t* path, int& width, int& height)
    {
        HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        uint8_t header[24] = {};
        DWORD read = 0;
        const BOOL ok = ::ReadFile(file, header, sizeof(header), &read, nullptr);
        ::CloseHandle(file);

        static const uint8_t pngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (!ok || read < sizeof(header) ||
            std::memcmp(header, pngSig, 8) != 0 ||
            std::memcmp(header + 12, "IHDR", 4) != 0)
        {
            return false;
        }

        const auto be32 = [](const uint8_t* p) -> uint32_t
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        };

        width = static_cast<int>(be32(header + 16));
        height = static_cast<int>(be32(header + 20));
        return width > 0 && height > 0;
    }

    /**
     * @brief 查询 PNG 覆盖帧尺寸（Data\Defs\<defName>\<group>_<frame>.png）
     *
     * 与 ERA 的 PNG 帧覆盖机制对应；采用 PNG 完整尺寸（IHDR）近似其
     * 透明边缘裁剪后的尺寸（不解析像素）。结果（含"不存在"）带缓存。
     */
    static bool getPngFrameMetrics(const CSprite* sprite, int group, int frame, int& width, int& height)
    {
        if (!sprite || gBaseDir[0] == L'\0' || group < 0 || frame < 0)
            return false;

        char key[96];
        std::snprintf(key, sizeof(key), "%s|%d|%d", sprite->Name.data(), group, frame);

        static std::unordered_map<std::string, std::pair<int, int>> cache;
        const auto it = cache.find(key);
        if (it != cache.end())
        {
            width = it->second.first;
            height = it->second.second;
            return width > 0;
        }

        wchar_t defWide[64] = {};
        const int wideLen = ::MultiByteToWideChar(CP_ACP, 0, sprite->Name.data(), -1, defWide, 64);
        if (wideLen <= 1)
        {
            cache.emplace(key, std::make_pair(0, 0));
            return false;
        }

        wchar_t path[MAX_PATH] = {};
        swprintf_s(path, L"%s\\Data\\Defs\\%s\\%d_%d.png",
            gBaseDir, defWide, group, frame);

        int w = 0;
        int h = 0;
        if (!readPngSize(path, w, h))
            w = h = 0;

        cache.emplace(key, std::make_pair(w, h));
        width = w;
        height = h;
        return w > 0;
    }

    // ============================================================
    // 资源缓存与帧尺寸
    // ============================================================

    CSprite* loadSpriteCached(const std::string& defName)
    {
        static std::unordered_map<std::string, CSprite*> cache;

        const auto it = cache.find(defName);
        if (it != cache.end())
            return it->second;

        // 加载失败也写入缓存（nullptr），避免每帧反复命中资源系统
        CSprite* sprite = ResourceManager::GetSprite(defName.c_str());
        cache.emplace(defName, sprite);
        return sprite;
    }

    bool getFrameMetrics(const CSprite* sprite, int group, int frame, int& width, int& height)
    {
        if (!sprite || group < 0 || frame < 0)
            return false;

        // PNG 覆盖帧优先（与 ERA GetDefFrameCroppingRect 一致）
        if (getPngFrameMetrics(sprite, group, frame, width, height))
            return true;

        if (group >= sprite->numSequences)
            return false;

        const CSequence* seq = sprite->s[group];
        if (!seq || frame >= seq->numFrames)
            return false;

        const CSpriteFrame* f = seq->f[frame];
        if (!f)
            return false;

        width = f->CroppedWidth;
        height = f->CroppedHeight;
        return width > 0 && height > 0;
    }

    int nbspWidth(const font& f)
    {
        const auto& abc = f.fr.abc[0xA0];
        return std::max(1, abc.abcA + abc.abcB + abc.abcC);
    }

    // ============================================================
    // 矩形裁剪（移植 ERA GraphTypes.RefineDrawBox）
    // ============================================================

    namespace
    {
        struct IntRect
        {
            int left;
            int top;
            int right;
            int bottom;
        };

        bool isRectEmpty(const IntRect& r)
        {
            return r.right <= r.left || r.bottom <= r.top;
        }

        void offsetRect(IntRect& r, int dx, int dy)
        {
            r.left += dx;
            r.right += dx;
            r.top += dy;
            r.bottom += dy;
        }

        IntRect boundsRect(int x, int y, int w, int h)
        {
            return IntRect{ x, y, x + w, y + h };
        }

        bool intersectRect(IntRect& out, const IntRect& a, const IntRect& b)
        {
            out.left = std::max(a.left, b.left);
            out.top = std::max(a.top, b.top);
            out.right = std::min(a.right, b.right);
            out.bottom = std::min(a.bottom, b.bottom);
            return !isRectEmpty(out);
        }

        void mirrorBoxHorizontally(IntRect& box, const IntRect& environment)
        {
            const int shift = (box.right - environment.right) + (box.left - environment.left);
            offsetRect(box, -shift, 0);
        }

        void mirrorBoxVertically(IntRect& box, const IntRect& environment)
        {
            const int shift = (box.bottom - environment.bottom) + (box.top - environment.top);
            offsetRect(box, 0, -shift);
        }

        /**
         * @brief 对源/目标绘制矩形做边界与镜像裁剪（对应 ERA GraphTypes.RefineDrawBox）
         *
         * SrcBox/DstBox 为环境边界（源图 / 目标图）；在输出参数中返回裁剪后的
         * 源起点、目标起点与绘制尺寸。
         */
        bool refineDrawBox(int& srcX, int& srcY, int& dstX, int& dstY, int& boxW, int& boxH,
            const IntRect& srcBox, const IntRect& dstBox, bool mirrorH, bool mirrorV)
        {
            if (boxW <= 0 || boxH <= 0 || isRectEmpty(srcBox) || isRectEmpty(dstBox))
                return false;

            const int srcDstOffsetX = dstX - srcX;
            const int srcDstOffsetY = dstY - srcY;

            // 将目标框对齐到源坐标系，便于统一求交
            IntRect movedDst = dstBox;
            offsetRect(movedDst, -srcDstOffsetX, -srcDstOffsetY);

            const IntRect srcDrawBox = boundsRect(srcX, srcY, boxW, boxH);

            IntRect correctedSrc;
            if (!intersectRect(correctedSrc, srcBox, srcDrawBox))
                return false;

            IntRect correctedDst;
            if (mirrorH || mirrorV)
            {
                correctedDst = correctedSrc;
                if (mirrorH)
                    mirrorBoxHorizontally(correctedDst, srcDrawBox);
                if (mirrorV)
                    mirrorBoxVertically(correctedDst, srcDrawBox);

                if (!intersectRect(correctedDst, correctedDst, movedDst))
                    return false;

                correctedSrc = correctedDst;
                if (mirrorH)
                    mirrorBoxHorizontally(correctedSrc, srcDrawBox);
                if (mirrorV)
                    mirrorBoxVertically(correctedSrc, srcDrawBox);
            }
            else
            {
                if (!intersectRect(correctedSrc, correctedSrc, movedDst))
                    return false;
                correctedDst = correctedSrc;
            }

            offsetRect(correctedDst, srcDstOffsetX, srcDstOffsetY);

            srcX = correctedSrc.left;
            srcY = correctedSrc.top;
            boxW = correctedSrc.right - correctedSrc.left;
            boxH = correctedSrc.bottom - correctedSrc.top;
            dstX = correctedDst.left;
            dstY = correctedDst.top;
            return true;
        }
    } // namespace

    // ============================================================
    // 绘制
    // ============================================================

    void drawSpriteFrame(CSprite* sprite, int group, int frame,
        int srcX, int srcY, int srcW, int srcH,
        Bitmap16Bit* pcx, int dstX, int dstY, bool mirror)
    {
        if (!sprite || !pcx || !pcx->map)
            return;

        // DDF_CROP：限制在帧裁剪区域内，并叠加帧裁剪偏移（帧内坐标 → 整图坐标）
        const int frameW = sprite->GetCroppedWidth(group, frame);
        const int frameH = sprite->GetCroppedHeight(group, frame);
        if (frameW <= 0 || frameH <= 0)
            return;

        if (srcW <= 0)
            srcW = frameW;
        if (srcH <= 0)
            srcH = frameH;

        srcW = std::min(srcW, frameW - srcX);
        srcH = std::min(srcH, frameH - srcY);
        if (srcW <= 0 || srcH <= 0)
            return;

        srcX += sprite->GetCroppedX(group, frame);
        srcY += sprite->GetCroppedY(group, frame);

        // 源边界为整图，目标边界为 PCX
        const IntRect srcBox = boundsRect(0, 0, sprite->Width, sprite->Height);
        const IntRect dstBox = boundsRect(0, 0, pcx->Width, pcx->Height);
        if (!refineDrawBox(srcX, srcY, dstX, dstY, srcW, srcH, srcBox, dstBox, mirror, false))
            return;

        // 镜像：源 X 翻转为整图坐标（对应 ERA：SrcX := Def.Width - SrcX - BoxWidth）
        if (mirror)
            srcX = sprite->Width - srcX - srcW;

        sprite->Draw(group, frame, srcX, srcY, srcW, srcH,
            pcx->map, dstX, dstY, pcx->Width, pcx->Height, pcx->Pitch, mirror, true);
    }

} // namespace H3FontExtension
