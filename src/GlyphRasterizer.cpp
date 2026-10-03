//===----------------------------------------------------------------------===//
// 字形光栅化后端工厂
//
// 运行时按平台能力选择实现：DirectWrite（Windows 7+）优先，GDI 兜底（XP）。
//===----------------------------------------------------------------------===//

#include "GlyphRasterizer.h"

namespace H3FontExtension
{
    std::unique_ptr<IGlyphRasterizer> CreateGlyphRasterizer()
    {
        // Windows 7 及以上：优先 DirectWrite（dwrite.dll 动态加载，无导入表依赖）
        if (auto dwrite = CreateDWriteGlyphRasterizer())
            return dwrite;

        // Windows XP：GDI 路径
        return CreateGdiGlyphRasterizer();
    }

} // namespace H3FontExtension
