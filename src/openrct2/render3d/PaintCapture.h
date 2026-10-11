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
#include "../interface/ScreenCoords.hpp"
#include "../world/Location.hpp"

#include <cstdint>
#include <vector>

struct PaintSession;
struct PaintStruct;
enum class ViewportInteractionItem : uint8_t;

namespace OpenRCT2
{
    struct EntityBase;
    struct TileElement;
} // namespace OpenRCT2

namespace OpenRCT2::Render3D
{
    // One sprite emitted by the game's isometric painter, together with the world-space bounding box the painter
    // sorts it by. This is what lets the 3D view place any object the game can draw without knowing its type.
    struct CapturedSprite
    {
        ImageId image;
        // Where the sprite origin is drawn in the isometric view the sprite was captured for.
        ScreenCoordsXY screenPos;
        // Inclusive bounding box in game coordinates, normalised so that min <= max.
        CoordsXYZ boundsMin;
        CoordsXYZ boundsMax;
        // Tile the painter was drawing when it emitted the sprite.
        CoordsXY mapPos;
        ViewportInteractionItem interaction;
        const TileElement* element;
        const EntityBase* entity;
    };

    // Runs the game's painter as seen from one of the four isometric view rotations and records the sprites it emits.
    class PaintCapture
    {
    private:
        PaintSession* _session = nullptr;
        uint8_t _rotation;

    public:
        explicit PaintCapture(uint8_t rotation);
        ~PaintCapture();

        PaintCapture(const PaintCapture&) = delete;
        PaintCapture& operator=(const PaintCapture&) = delete;

        uint8_t GetRotation() const
        {
            return _rotation;
        }

        // Paints the tile elements (paths, scenery, walls, track, ...) of a tile.
        void AddTile(const TileCoordsXY& tile);
        // Paints the entities (guests, staff, vehicles, ...) standing on a tile.
        void AddEntities(const TileCoordsXY& tile);

        // Appends everything painted so far to `out` and starts over.
        void Collect(std::vector<CapturedSprite>& out);

    private:
        void Reset();
        void CollectStruct(const PaintStruct& ps, std::vector<CapturedSprite>& out) const;
    };
} // namespace OpenRCT2::Render3D
