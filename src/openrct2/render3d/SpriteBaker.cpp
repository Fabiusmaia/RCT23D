/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SpriteBaker.h"

#include "../drawing/Drawing.Sprite.h"
#include "../drawing/G1Element.h"
#include "../drawing/RenderTarget.h"
#include "../world/Location.hpp"

#include <algorithm>
#include <cmath>

namespace OpenRCT2::Render3D
{
    using namespace OpenRCT2::Drawing;

    uint64_t GetImageKey(ImageId image)
    {
        uint64_t flags = 0;
        if (image.HasPrimary())
            flags |= 1;
        if (image.HasSecondary())
            flags |= 2;
        if (image.HasTertiary())
            flags |= 4;
        if (image.IsBlended())
            flags |= 8;
        if (image.IsRemap())
            flags |= 16;
        return static_cast<uint64_t>(image.GetIndex()) | (static_cast<uint64_t>(EnumValue(image.GetPrimary())) << 32)
            | (static_cast<uint64_t>(EnumValue(image.GetSecondary())) << 40)
            | (static_cast<uint64_t>(EnumValue(image.GetTertiary())) << 48) | (flags << 56);
    }

    bool RasterizeSprite(ImageId image, IndexedImage& out)
    {
        if (!image.HasValue())
            return false;

        const auto* g1 = GfxGetG1Element(image);
        if (g1 == nullptr || g1->offset == nullptr || g1->width <= 0 || g1->height <= 0)
            return false;

        // Guard against corrupt image data producing huge allocations.
        if (g1->width > 4096 || g1->height > 4096)
            return false;

        out.width = g1->width;
        out.height = g1->height;
        out.originX = -g1->xOffset;
        out.originY = -g1->yOffset;
        out.pixels.assign(static_cast<size_t>(out.width) * out.height, 0);

        RenderTarget rt;
        rt.bits = reinterpret_cast<PaletteIndex*>(out.pixels.data());
        rt.x = 0;
        rt.y = 0;
        rt.width = out.width;
        rt.height = out.height;
        rt.pitch = 0;
        rt.zoom_level = ZoomLevel{ 0 };
        GfxDrawSpriteSoftware(rt, image, { out.originX, out.originY });
        return true;
    }

    IndexedImage Scale2x(const IndexedImage& image)
    {
        IndexedImage result;
        result.width = image.width * 2;
        result.height = image.height * 2;
        result.originX = image.originX * 2;
        result.originY = image.originY * 2;
        result.pixels.assign(static_cast<size_t>(result.width) * result.height, 0);

        // The image border is only where the sprite was cropped, not an outline, so neighbours outside it repeat the
        // edge pixel instead of counting as transparent (which would nibble the corners of anything touching the edge).
        auto at = [&image](int32_t x, int32_t y) {
            return image.At(std::clamp(x, 0, image.width - 1), std::clamp(y, 0, image.height - 1));
        };
        for (int32_t y = 0; y < image.height; y++)
        {
            for (int32_t x = 0; x < image.width; x++)
            {
                const uint8_t b = at(x, y - 1);
                const uint8_t d = at(x - 1, y);
                const uint8_t e = at(x, y);
                const uint8_t f = at(x + 1, y);
                const uint8_t h = at(x, y + 1);

                uint8_t e0 = e, e1 = e, e2 = e, e3 = e;
                if (b != h && d != f)
                {
                    e0 = d == b ? d : e;
                    e1 = b == f ? f : e;
                    e2 = d == h ? d : e;
                    e3 = h == f ? f : e;
                }

                const size_t row0 = static_cast<size_t>(y * 2) * result.width;
                const size_t row1 = row0 + result.width;
                result.pixels[row0 + x * 2] = e0;
                result.pixels[row0 + x * 2 + 1] = e1;
                result.pixels[row1 + x * 2] = e2;
                result.pixels[row1 + x * 2 + 1] = e3;
            }
        }
        return result;
    }

    IndexedImage ProjectGroundSprite(
        const IndexedImage& sprite, const TileCornerOffsets& corners, int32_t resolution, int32_t spriteScale)
    {
        IndexedImage result;
        result.width = resolution;
        result.height = resolution;
        result.pixels.assign(static_cast<size_t>(resolution) * resolution, 0);

        const float step = static_cast<float>(kCoordsXYStep) / resolution;
        for (int32_t j = 0; j < resolution; j++)
        {
            const float y = (j + 0.5f) * step;
            const float fy = y / kCoordsXYStep;
            for (int32_t i = 0; i < resolution; i++)
            {
                const float x = (i + 0.5f) * step;
                const float fx = x / kCoordsXYStep;

                // Corners: south (0,0), east (32,0), north (32,32), west (0,32).
                const float zSouthEast = corners.south + (corners.east - corners.south) * fx;
                const float zWestNorth = corners.west + (corners.north - corners.west) * fx;
                const float z = zSouthEast + (zWestNorth - zSouthEast) * fy;

                // Translate3DTo2DWithZ for rotation 0.
                const float screenX = y - x;
                const float screenY = (x + y) * 0.5f - z;

                const auto px = static_cast<int32_t>(std::floor(screenX * spriteScale)) + sprite.originX;
                const auto py = static_cast<int32_t>(std::floor(screenY * spriteScale)) + sprite.originY;
                result.pixels[static_cast<size_t>(j) * resolution + i] = sprite.At(px, py);
            }
        }
        return result;
    }
} // namespace OpenRCT2::Render3D
