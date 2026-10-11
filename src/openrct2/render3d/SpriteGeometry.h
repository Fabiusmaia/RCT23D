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
    struct ScreenPointF
    {
        float x{};
        float y{};
    };

    // Floating point version of Translate3DTo2DWithZ: where a game coordinate (x/y 32 per tile, z in game height units)
    // appears in the isometric view with the given rotation.
    ScreenPointF ProjectIsometric(uint8_t rotation, float x, float y, float z);

    // Rotates a game x/y vector the same way CoordsXY::rotate does.
    void RotateXY(uint8_t rotation, float x, float y, float& outX, float& outY);

    // A sprite as the isometric painter placed it, in game coordinates.
    struct SpritePlacement
    {
        uint8_t rotation{};
        // Screen rectangle covered by the sprite's pixels.
        float left{};
        float top{};
        float width{};
        float height{};
        // Painter bounding box (game units, min <= max).
        Vec3 boundsMin{};
        Vec3 boundsMax{};
    };

    // Plane z = baseZ + gradientX * x + gradientY * y in game units, used for sprites that lie on the ground.
    struct GroundPlane
    {
        float baseZ{};
        float gradientX{};
        float gradientY{};
    };

    // Bounding boxes this thin are treated as lying flat (paths, litter, flower beds, ...).
    constexpr float kFlatSpriteMaxHeight = 3.0f;

    // How far (game z units) a box may be raised to fit its sprite before the sprite is treated as standing up
    // (front faces only) rather than having a roof.
    constexpr float kMaxTopFaceRaise = 12.0f;

    // Projects the sprite onto the faces of its bounding box that face the isometric camera, so that seen from that
    // direction it looks exactly like the 2D game while still having depth. The box is grown backwards and upwards
    // as needed so that no pixel of the sprite is lost. A sprite much taller than its box puts the part above the box on
    // a vertical card across the middle of the box.
    void AddSpriteBox(std::vector<Vertex>& out, const SpritePlacement& placement, const TextureRegion& texture);

    // How far a box has to grow to hold the sprite, in game units: horizontally (backwards) and upwards.
    struct SpriteBoxGrowth
    {
        float horizontal{};
        float vertical{};
    };
    SpriteBoxGrowth MeasureSpriteBoxGrowth(const SpritePlacement& placement);

    // Whether the painter's box is too far from the sprite to project onto (the sprite is drawn well away from where
    // its box says the object is). Such sprites are drawn on a vertical card instead.
    bool IsSpriteBoxMismatched(const SpritePlacement& placement);

    // Projects the sprite onto a vertical card through the middle of its box, facing the isometric camera.
    void AddSpriteCard(std::vector<Vertex>& out, const SpritePlacement& placement, const TextureRegion& texture);

    // Projects the sprite onto a ground plane, covering exactly the sprite's pixels.
    void AddSpriteOnPlane(
        std::vector<Vertex>& out, const SpritePlacement& placement, const GroundPlane& plane, const TextureRegion& texture);

    // Vertex flag for geometry textured with game sprites, which already contain the game's lighting.
    constexpr float kVertexFlagSprite = 2.0f;
} // namespace OpenRCT2::Render3D
