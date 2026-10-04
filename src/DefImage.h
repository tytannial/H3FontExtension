//===----------------------------------------------------------------------===//
// ERA EML 内嵌 DEF 图片支持：资源加载缓存、帧尺寸与绘制（含裁剪/镜像）
//
// 对应 ERA 的 Graph.DrawInterfaceDefFrameEx / GetDefFrameCroppingRect 语义：
//   - 资源：GetSprite(name)（游戏 .def → CSprite）
//   - 尺寸：帧裁剪矩形（CroppedX/Y/Width/Height）
//   - 绘制：CSprite::Draw（游戏 DEF 绘制函数），调用前做 RefineDrawBox 裁剪
//===----------------------------------------------------------------------===//

#pragma once

#include "H3FontExtension.h"

namespace H3FontExtension
{
    /**
     * @brief 设置游戏目录（用于 Data\Defs PNG 覆盖帧查找；插件 init 时调用一次）
     *
     * 插件以 BinkW32 代理形式位于游戏根目录，传入模块目录即可。
     */
    void setSpriteBaseDir(const wchar_t* dir);

    /**
     * @brief 加载 DEF 资源（带缓存；失败同样缓存 nullptr，避免反复命中资源系统）
     *
     * @param defName DEF 资源名（如 "adventure.def"）
     * @return 游戏 sprite 对象；加载失败返回 nullptr
     */
    CSprite* loadSpriteCached(const std::string& defName);

    /**
     * @brief 查询帧裁剪尺寸（帧在整图中的有效区域，对应 ERA GetDefFrameCroppingRect）
     *
     * @param sprite DEF 资源
     * @param group  帧组索引
     * @param frame  帧索引
     * @param width  [out] 帧有效宽度
     * @param height [out] 帧有效高度
     * @return 索引合法且尺寸有效时为 true
     */
    bool getFrameMetrics(const CSprite* sprite, int group, int frame, int& width, int& height);

    /**
     * @brief 将 DEF 帧（帧内坐标 + 切片 + 镜像）绘制到 PCX 缓冲
     *
     * 与 ERA DrawInterfaceDefFrameEx(..., DDF_CROP) 等价：
     *   1. 源尺寸限制在帧裁剪区域内，并叠加帧裁剪偏移（帧内坐标 → 整图坐标）
     *   2. RefineDrawBox 对源/目标矩形做边界与镜像裁剪
     *   3. 镜像时源 X 翻转为整图坐标，最后交给游戏绘制函数（16/32 位由其适配层处理）
     *
     * @param sprite      DEF 资源
     * @param group/frame 帧索引
     * @param srcX/srcY   帧内起点（相对帧裁剪区域左上角）
     * @param srcW/srcH   切片尺寸（<=0 时取帧裁剪尺寸的剩余部分）
     * @param pcx         目标 PCX 缓冲
     * @param dstX/dstY   目标坐标
     * @param mirror      水平镜像
     */
    void drawSpriteFrame(CSprite* sprite, int group, int frame,
        int srcX, int srcY, int srcW, int srcH,
        Bitmap16Bit* pcx, int dstX, int dstY, bool mirror);

    /** @brief NBSP 占位宽度（与原版字体表中 0xA0 的步进一致，最小 1） */
    int nbspWidth(const font& f);

} // namespace H3FontExtension
