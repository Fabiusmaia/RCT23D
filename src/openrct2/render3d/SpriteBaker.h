/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "SceneTypes.h"

#include <cstdint>
#include <vector>

namespace OpenRCT2::Render3D
{
    // 8-bit palette indexed image, index 0 is transparent.
    struct IndexedImage
    {
        int32_t width{};
        int32_t height{};
        // Position of the sprite origin within the image (the negated G1 offsets).
        int32_t originX{};
        int32_t originY{};
        std::vector<uint8_t> pixels;

        uint8_t At(int32_t x, int32_t y) const
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
            return pixels[static_cast<size_t>(y) * width + x];
        }
    };

    // Draws a game sprite (with its colour remaps) into an indexed image. Returns false if the image does not exist.
    bool RasterizeSprite(ImageId image, IndexedImage& out);

    // Doubles an image with the Scale2x (AdvMAME2x) pixel art filter: diagonal edges become smooth while flat areas
    // and colours stay exactly as drawn. Works on palette indices, so no colours are invented. Origin is doubled too.
    IndexedImage Scale2x(const IndexedImage& image);

    // Re-projects an isometric tile-top sprite (drawn at the tile origin in view rotation 0) onto a square texture.
    // Texel column i maps to world x, row j to world y. `corners` are the surface heights relative to the sprite origin.
    // `spriteScale` is how many image pixels there are per game pixel (2 after Scale2x).
    IndexedImage ProjectGroundSprite(
        const IndexedImage& sprite, const TileCornerOffsets& corners, int32_t resolution, int32_t spriteScale = 1);
} // namespace OpenRCT2::Render3D
