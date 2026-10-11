/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "WalkCollision.h"

#include "../object/SmallSceneryEntry.h"
#include "../world/Map.h"
#include "../world/Scenery.h"
#include "../world/tile_element/PathElement.h"
#include "../world/tile_element/SmallSceneryElement.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"
#include "../world/tile_element/WallElement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenRCT2::Render3D
{
    namespace
    {
        CoordsXY TileOf(float x, float y)
        {
            return CoordsXY{ static_cast<int32_t>(std::floor(x)), static_cast<int32_t>(std::floor(y)) }.toTileStart();
        }

        // Height of a footpath's walking surface at a point on its tile.
        float PathHeightAt(const PathElement& path, float localX, float localY)
        {
            float z = static_cast<float>(path.getBaseZ());
            if (!path.isSloped())
                return z;
            // Sloped paths rise one land step (16 units) across the tile towards their slope direction.
            float fraction = 0;
            switch (path.getSlopeDirection() & 3)
            {
                case 0:
                    fraction = (kCoordsXYStep - localX) / kCoordsXYStep;
                    break;
                case 1:
                    fraction = localY / kCoordsXYStep;
                    break;
                case 2:
                    fraction = localX / kCoordsXYStep;
                    break;
                case 3:
                    fraction = (kCoordsXYStep - localY) / kCoordsXYStep;
                    break;
            }
            return z + 16.0f * std::clamp(fraction, 0.0f, 1.0f);
        }

        // Which of the four scenery quadrants of its tile a point is in (see SceneryQuadrantOffsets).
        uint8_t QuadrantAt(float localX, float localY)
        {
            const bool highX = localX >= kCoordsXYHalfTile;
            const bool highY = localY >= kCoordsXYHalfTile;
            if (!highX)
                return highY ? 1 : 0;
            return highY ? 2 : 3;
        }

        bool Overlaps(int32_t elementBottom, int32_t elementTop, float z0, float z1)
        {
            return static_cast<float>(elementBottom) < z1 && static_cast<float>(elementTop) > z0;
        }
    } // namespace

    bool FindWalkableGround(float x, float y, float maxZ, float& outZ)
    {
        const auto tile = TileOf(x, y);
        if (!MapIsLocationValid(tile) || MapIsEdge(tile))
            return false;

        const float localX = x - static_cast<float>(tile.x);
        const float localY = y - static_cast<float>(tile.y);
        float best = -std::numeric_limits<float>::infinity();

        const TileElement* element = MapGetFirstElementAt(tile);
        if (element == nullptr)
            return false;
        do
        {
            if (element->isGhost())
                continue;
            switch (element->getType())
            {
                case TileElementType::surface:
                {
                    const auto* surface = element->asSurface();
                    const float ground = TileElementHeight(CoordsXY{ static_cast<int32_t>(x), static_cast<int32_t>(y) });
                    // Water deeper than a puddle cannot be walked on.
                    if (surface->getWaterHeight() > ground + 4.0f)
                        break;
                    if (ground <= maxZ)
                        best = std::max(best, ground);
                    break;
                }
                case TileElementType::path:
                {
                    const float pathZ = PathHeightAt(*element->asPath(), localX, localY);
                    if (pathZ <= maxZ)
                        best = std::max(best, pathZ);
                    break;
                }
                default:
                    break;
            }
        } while (!(element++)->isLastForTile());

        if (best == -std::numeric_limits<float>::infinity())
            return false;
        outZ = best;
        return true;
    }

    bool IsSpaceBlocked(float x, float y, float z0, float z1)
    {
        const auto tile = TileOf(x, y);
        if (!MapIsLocationValid(tile))
            return true;

        const float localX = x - static_cast<float>(tile.x);
        const float localY = y - static_cast<float>(tile.y);

        const TileElement* element = MapGetFirstElementAt(tile);
        if (element == nullptr)
            return false;
        do
        {
            if (element->isGhost() || element->isInvisible())
                continue;
            const int32_t bottom = element->getBaseZ();
            const int32_t top = element->getClearanceZ();
            switch (element->getType())
            {
                case TileElementType::track:
                case TileElementType::largeScenery:
                    if (Overlaps(bottom, top, z0, z1))
                        return true;
                    break;
                case TileElementType::smallScenery:
                {
                    if (!Overlaps(bottom, top, z0, z1))
                        break;
                    const auto* scenery = element->asSmallScenery();
                    const auto* entry = scenery->getEntry();
                    if (entry == nullptr || entry->flags.has(SmallSceneryFlag::occupiesFullTile))
                        return true;
                    if (scenery->getSceneryQuadrant() == QuadrantAt(localX, localY))
                        return true;
                    break;
                }
                default:
                    break;
            }
        } while (!(element++)->isLastForTile());
        return false;
    }

    bool IsEdgeBlocked(float fromX, float fromY, float toX, float toY, float z0, float z1)
    {
        const auto from = TileOf(fromX, fromY);
        const auto to = TileOf(toX, toY);
        if (from == to)
            return false;

        // Walls stand on one tile's edge facing out; check the edge on both sides of the step.
        auto blockedBy = [&](const CoordsXY& tile, CoordsXY towards) {
            if (!MapIsLocationValid(tile))
                return false;
            const TileElement* element = MapGetFirstElementAt(tile);
            if (element == nullptr)
                return false;
            do
            {
                if (element->getType() != TileElementType::wall || element->isGhost())
                    continue;
                const auto edge = CoordsDirectionDelta[element->getDirection() & 3];
                const bool facing = (towards.x != 0 && edge.x == towards.x) || (towards.y != 0 && edge.y == towards.y);
                if (facing && Overlaps(element->getBaseZ(), element->getClearanceZ(), z0, z1))
                    return true;
            } while (!(element++)->isLastForTile());
            return false;
        };

        const CoordsXY step{ to.x - from.x, to.y - from.y };
        return blockedBy(from, step) || blockedBy(to, CoordsXY{ -step.x, -step.y });
    }

    bool TryWalk(const Vec3& feet, float toX, float toY, float& outGroundZ)
    {
        const float feetZ = WorldZToGame(feet.z);
        float ground{};
        if (!FindWalkableGround(toX, toY, feetZ + kWalkStepHeight, ground))
            return false;

        const float stand0 = ground + 4.0f;
        const float stand1 = ground + kWalkHeadroom;
        if (IsSpaceBlocked(toX, toY, stand0, stand1))
        {
            // Never trap someone who is already standing inside an object (e.g. after switching to walking there).
            if (!IsSpaceBlocked(feet.x, feet.y, feetZ + 4.0f, feetZ + kWalkHeadroom))
                return false;
        }
        if (IsEdgeBlocked(feet.x, feet.y, toX, toY, std::max(feetZ, ground) + 2.0f, std::max(feetZ, ground) + kWalkHeadroom))
            return false;

        outGroundZ = GameToWorld(0, 0, ground).z;
        return true;
    }
} // namespace OpenRCT2::Render3D
