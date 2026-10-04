//===----------------------------------------------------------------------===//
// ERA EML 内嵌 DEF 图片：资源缓存、帧尺寸与裁剪绘制实现
//===----------------------------------------------------------------------===//

#include "DefImage.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>

namespace H3FontExtension
{
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
