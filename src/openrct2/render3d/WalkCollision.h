/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "Math3D.h"

#include <cstdint>

namespace OpenRCT2::Render3D
{
    // Heights in game z units.
    // Highest step a walker can climb without a path: one land height step and a little.
    constexpr int32_t kWalkStepHeight = 18;
    // Height of a person; anything solid in this range blocks the way.
    constexpr int32_t kWalkHeadroom = 32;

    // Highest walkable surface (terrain or footpath) at a game x/y that is no higher than `maxZ`, in game z units.
    // Returns false if there is none (cliff, water without a path, outside the map).
    bool FindWalkableGround(float x, float y, float maxZ, float& outZ);

    // Whether rides, scenery or buildings occupy the space a person standing at x/y between z0 and z1 would take.
    bool IsSpaceBlocked(float x, float y, float z0, float z1);

    // Whether walls or fences on the edge between two tiles block walking from one to the other at this height.
    bool IsEdgeBlocked(float fromX, float fromY, float toX, float toY, float z0, float z1);

    // The walking collision used by the 3D camera (world units, see Camera3D::WalkFunc).
    bool TryWalk(const Vec3& feet, float toX, float toY, float& outGroundZ);
} // namespace OpenRCT2::Render3D
