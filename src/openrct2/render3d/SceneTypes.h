/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../drawing/ImageId.hpp"
#include "Math3D.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace OpenRCT2::Render3D
{
    // Number of tiles along each side of a mesh chunk.
    constexpr int32_t kChunkSize = 16;

    enum class VertexFlag : uint8_t
    {
        // Regular geometry positioned at `position`.
        none = 0,
        // Cylindrical billboard: `position` is the anchor, `offset` is applied along the camera right/up axes.
        billboard = 1,
    };

    // Only visible when the camera's nearest isometric rotation matches. Used to pick one of the four pre-rendered
    // sprite views for billboards.
    constexpr float kAnyRotation = -1.0f;

    // Must match the attribute layout in render3d.vert. Sprite geometry (flags == 2) has no use for the normal and offset,
    // so they hold the sprite's atlas rectangle instead: offset = (u0, v0), normal = (u1, v1, 0).
    struct Vertex
    {
        Vec3 position;
        Vec3 normal;
        float u{};
        float v{};
        // Texture array layer, negative for untextured geometry.
        float layer = -1.0f;
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
        float offsetX{};
        float offsetY{};
        float rotation = kAnyRotation;
        float flags{};
    };
    static_assert(sizeof(Vertex) == 56);

    struct Colour4
    {
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
    };

    struct TextureRegion
    {
        float u0{};
        float v0{};
        float u1{};
        float v1{};
        float layer = -1.0f;
        // Average colour of the opaque pixels, useful for untextured stand-ins.
        Colour4 average{};

        bool IsValid() const
        {
            return layer >= 0.0f;
        }
    };

    // Corner heights (game z units) relative to the base height, used to project ground sprites onto a tile.
    struct TileCornerOffsets
    {
        int16_t north{};
        int16_t east{};
        int16_t south{};
        int16_t west{};
    };

    // Supplies textures to the scene builder. Implemented by the renderer, which owns the GPU resources.
    struct ITextureProvider
    {
        virtual ~ITextureProvider() = default;

        // The whole sprite as drawn by the game, for billboards.
        virtual TextureRegion GetSprite(ImageId image) = 0;

        // A tile-top sprite (terrain or path) re-projected from the isometric view onto a square covering one tile.
        // Texture u runs along world x, v along world y.
        virtual TextureRegion GetGroundProjection(ImageId image, const TileCornerOffsets& corners) = 0;

        // The current colour of a palette entry.
        virtual Colour4 GetPaletteColour(uint8_t paletteIndex) = 0;
    };

    struct MeshData
    {
        // Opaque and alpha-tested geometry.
        std::vector<Vertex> solid;
        // Blended geometry, drawn after solid without depth writes.
        std::vector<Vertex> transparent;
        AABB bounds{ { 1e9f, 1e9f, 1e9f }, { -1e9f, -1e9f, -1e9f } };

        void Clear()
        {
            solid.clear();
            transparent.clear();
            bounds = { { 1e9f, 1e9f, 1e9f }, { -1e9f, -1e9f, -1e9f } };
        }

        bool Empty() const
        {
            return solid.empty() && transparent.empty();
        }

        void Extend(const Vec3& p, float radius = 0.0f)
        {
            bounds.min.x = std::min(bounds.min.x, p.x - radius);
            bounds.min.y = std::min(bounds.min.y, p.y - radius);
            bounds.min.z = std::min(bounds.min.z, p.z - radius);
            bounds.max.x = std::max(bounds.max.x, p.x + radius);
            bounds.max.y = std::max(bounds.max.y, p.y + radius);
            bounds.max.z = std::max(bounds.max.z, p.z + radius);
        }
    };

    // Stable cache key for an image including its colour remaps.
    uint64_t GetImageKey(ImageId image);
} // namespace OpenRCT2::Render3D
