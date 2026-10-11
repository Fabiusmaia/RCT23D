/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PaintCapture.h"

#include "../interface/Viewport.h"
#include "../paint/Paint.Entity.h"
#include "../paint/Paint.h"
#include "../paint/tile_element/Paint.TileElement.h"

#include <algorithm>

namespace OpenRCT2::Render3D
{
    using namespace OpenRCT2::Drawing;

    namespace
    {
        // Large enough that nothing on any map is culled by the painter's screen bounds checks.
        constexpr int32_t kUnboundedOrigin = -(1 << 20);
        constexpr int32_t kUnboundedSize = 1 << 21;

        RenderTarget MakeUnboundedTarget()
        {
            RenderTarget rt;
            rt.bits = nullptr;
            rt.x = kUnboundedOrigin;
            rt.y = kUnboundedOrigin;
            rt.width = kUnboundedSize;
            rt.height = kUnboundedSize;
            rt.pitch = 0;
            rt.cullingX = kUnboundedOrigin;
            rt.cullingY = kUnboundedOrigin;
            rt.cullingWidth = kUnboundedSize;
            rt.cullingHeight = kUnboundedSize;
            rt.zoom_level = ZoomLevel{ 0 };
            return rt;
        }
    } // namespace

    PaintCapture::PaintCapture(uint8_t rotation)
        : _rotation(rotation & 3)
    {
        Reset();
    }

    PaintCapture::~PaintCapture()
    {
        if (_session != nullptr)
            PaintSessionFree(_session);
    }

    void PaintCapture::Reset()
    {
        if (_session != nullptr)
            PaintSessionFree(_session);
        auto rt = MakeUnboundedTarget();
        _session = PaintSessionAlloc(rt, ViewportFlags{}, _rotation);
    }

    void PaintCapture::AddTile(const TileCoordsXY& tile)
    {
        TileElementPaintSetup(*_session, tile.toCoordsXY());
    }

    void PaintCapture::AddEntities(const TileCoordsXY& tile)
    {
        EntityPaintSetup(*_session, tile.toCoordsXY());
    }

    void PaintCapture::CollectStruct(const PaintStruct& ps, std::vector<CapturedSprite>& out) const
    {
        CapturedSprite sprite;
        sprite.boundsMin = { std::min(ps.Bounds.x, ps.Bounds.x_end), std::min(ps.Bounds.y, ps.Bounds.y_end),
                             std::min(ps.Bounds.z, ps.Bounds.z_end) };
        sprite.boundsMax = { std::max(ps.Bounds.x, ps.Bounds.x_end), std::max(ps.Bounds.y, ps.Bounds.y_end),
                             std::max(ps.Bounds.z, ps.Bounds.z_end) };
        sprite.mapPos = ps.MapPos;
        sprite.interaction = ps.InteractionItem;
        sprite.element = ps.Element;
        sprite.entity = ps.Entity;
        sprite.image = ps.image_id;
        sprite.screenPos = ps.ScreenPos;
        out.push_back(sprite);

        // Mirrors PaintDrawStruct: children are drawn after their parent, attached images only at the end of the chain.
        if (ps.Children != nullptr)
        {
            CollectStruct(*ps.Children, out);
            return;
        }
        for (const auto* attached = ps.Attached; attached != nullptr; attached = attached->NextEntry)
        {
            // Masked attachments combine two images per pixel, which a single texture cannot represent; the colour
            // image alone is the closest match.
            sprite.image = attached->IsMasked ? attached->ColourImageId : attached->image_id;
            sprite.screenPos = ps.ScreenPos + attached->RelativePos;
            out.push_back(sprite);
        }
    }

    void PaintCapture::Collect(std::vector<CapturedSprite>& out)
    {
        auto& session = *_session;
        if (session.QuadrantBackIndex <= session.QuadrantFrontIndex)
        {
            for (uint32_t i = session.QuadrantBackIndex; i <= session.QuadrantFrontIndex; i++)
            {
                for (const PaintStruct* ps = session.Quadrants[i]; ps != nullptr; ps = ps->NextQuadrantEntry)
                {
                    CollectStruct(*ps, out);
                }
            }
        }
        Reset();
    }
} // namespace OpenRCT2::Render3D
