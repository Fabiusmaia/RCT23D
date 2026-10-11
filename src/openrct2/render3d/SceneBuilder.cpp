/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SceneBuilder.h"

#include "../GameState.h"
#include "../SpriteIds.h"
#include "../Diagnostic.h"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/G1Element.h"
#include "../interface/Viewport.h"
#include "../object/TerrainEdgeObject.h"
#include "../object/TerrainSurfaceObject.h"
#include "../world/Map.h"
#include "../drawing/Colour.h"
#include "../drawing/ColourMap.h"
#include "../entity/EntityList.h"
#include "../ride/Ride.h"
#include "../ride/Vehicle.h"
#include "../world/tile_element/PathElement.h"
#include "../world/tile_element/Slope.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"
#include "../world/tile_element/TrackElement.h"
#include "PaintCapture.h"
#include "SpriteGeometry.h"
#include "TrackGeometry.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace OpenRCT2::Render3D
{
    namespace
    {
        constexpr Colour4 kFallbackTerrainColour{ 96, 150, 64, 255 };
        constexpr Colour4 kFallbackCliffColour{ 120, 96, 72, 255 };
        constexpr Colour4 kWaterColour{ 70, 120, 190, 160 };

        Vec3 TriangleNormal(const Vec3& a, const Vec3& b, const Vec3& c)
        {
            return Normalize(Cross(b - a, c - a));
        }

        Vertex MakeVertex(const Vec3& p, const Vec3& n, Colour4 colour, const TextureRegion* texture, float u, float v)
        {
            Vertex vtx;
            vtx.position = p;
            vtx.normal = n;
            vtx.r = colour.r;
            vtx.g = colour.g;
            vtx.b = colour.b;
            vtx.a = colour.a;
            if (texture != nullptr && texture->IsValid())
            {
                vtx.layer = texture->layer;
                vtx.u = texture->u0 + (texture->u1 - texture->u0) * u;
                vtx.v = texture->v0 + (texture->v1 - texture->v0) * v;
            }
            return vtx;
        }

        Colour4 Shade(Colour4 c, float factor)
        {
            auto s = [factor](uint8_t v) { return static_cast<uint8_t>(std::clamp(v * factor, 0.0f, 255.0f)); };
            return { s(c.r), s(c.g), s(c.b), c.a };
        }

        void HashBytes(uint64_t& hash, const void* data, size_t len)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; i++)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        }

        const SurfaceElement* GetSurface(const TileCoordsXY& tile)
        {
            const auto& mapSize = getGameState().mapSize;
            if (tile.x < 0 || tile.y < 0 || tile.x >= mapSize.x || tile.y >= mapSize.y)
                return nullptr;
            return MapGetSurfaceElementAt(tile);
        }
    } // namespace

    bool SplitAlongEastWest(const TileCornersZ& corners)
    {
        // Fold along the diagonal whose opposite corners differ the most, so that a single raised or lowered corner
        // forms its own triangle. On a tie (flat or valley shapes) use the lower diagonal so valleys stay valleys.
        const int32_t northSouthDelta = std::abs(corners.north - corners.south);
        const int32_t eastWestDelta = std::abs(corners.east - corners.west);
        if (northSouthDelta != eastWestDelta)
            return northSouthDelta > eastWestDelta;
        return (corners.east + corners.west) <= (corners.north + corners.south);
    }

    void AddTriangle(
        std::vector<Vertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, Colour4 colour, const TextureRegion* texture,
        const std::array<float, 6>& uvs)
    {
        const Vec3 n = TriangleNormal(a, b, c);
        out.push_back(MakeVertex(a, n, colour, texture, uvs[0], uvs[1]));
        out.push_back(MakeVertex(b, n, colour, texture, uvs[2], uvs[3]));
        out.push_back(MakeVertex(c, n, colour, texture, uvs[4], uvs[5]));
    }

    void AddQuad(
        std::vector<Vertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, Colour4 colour,
        const TextureRegion* texture, const std::array<float, 8>* uvs)
    {
        static constexpr std::array<float, 8> kDefaultUvs = { 0, 0, 1, 0, 1, 1, 0, 1 };
        const auto& uv = uvs != nullptr ? *uvs : kDefaultUvs;
        // Use the normal of whichever triangle is not degenerate so trapezoids with a collapsed side still light correctly.
        Vec3 n = TriangleNormal(a, b, c);
        if (Length(n) < 0.5f)
            n = TriangleNormal(a, c, d);
        const Vertex va = MakeVertex(a, n, colour, texture, uv[0], uv[1]);
        const Vertex vb = MakeVertex(b, n, colour, texture, uv[2], uv[3]);
        const Vertex vc = MakeVertex(c, n, colour, texture, uv[4], uv[5]);
        const Vertex vd = MakeVertex(d, n, colour, texture, uv[6], uv[7]);
        out.push_back(va);
        out.push_back(vb);
        out.push_back(vc);
        out.push_back(va);
        out.push_back(vc);
        out.push_back(vd);
    }

    void AddBox(std::vector<Vertex>& out, const AABB& box, Colour4 colour)
    {
        const Vec3& lo = box.min;
        const Vec3& hi = box.max;
        const Vec3 p000{ lo.x, lo.y, lo.z };
        const Vec3 p100{ hi.x, lo.y, lo.z };
        const Vec3 p110{ hi.x, hi.y, lo.z };
        const Vec3 p010{ lo.x, hi.y, lo.z };
        const Vec3 p001{ lo.x, lo.y, hi.z };
        const Vec3 p101{ hi.x, lo.y, hi.z };
        const Vec3 p111{ hi.x, hi.y, hi.z };
        const Vec3 p011{ lo.x, hi.y, hi.z };
        // Counter-clockwise when seen from outside.
        AddQuad(out, p001, p101, p111, p011, colour);                 // top
        AddQuad(out, p000, p010, p110, p100, Shade(colour, 0.6f));    // bottom
        AddQuad(out, p000, p100, p101, p001, Shade(colour, 0.85f));   // -y
        AddQuad(out, p110, p010, p011, p111, Shade(colour, 0.85f));   // +y
        AddQuad(out, p100, p110, p111, p101, Shade(colour, 0.75f));   // +x
        AddQuad(out, p010, p000, p001, p011, Shade(colour, 0.75f));   // -x
    }

    int32_t SceneBuilder::GetChunkCountX()
    {
        return (getGameState().mapSize.x + kChunkSize - 1) / kChunkSize;
    }

    int32_t SceneBuilder::GetChunkCountY()
    {
        return (getGameState().mapSize.y + kChunkSize - 1) / kChunkSize;
    }

    uint64_t SceneBuilder::ComputeChunkHash(ChunkCoord chunk)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto& mapSize = getGameState().mapSize;
        const int32_t x0 = std::max(0, chunk.x * kChunkSize - 1);
        const int32_t y0 = std::max(0, chunk.y * kChunkSize - 1);
        const int32_t x1 = std::min<int32_t>(mapSize.x, (chunk.x + 1) * kChunkSize + 1);
        const int32_t y1 = std::min<int32_t>(mapSize.y, (chunk.y + 1) * kChunkSize + 1);
        for (int32_t y = y0; y < y1; y++)
        {
            for (int32_t x = x0; x < x1; x++)
            {
                const TileElement* element = MapGetFirstElementAt(TileCoordsXY{ x, y });
                if (element == nullptr)
                    continue;
                do
                {
                    HashBytes(hash, element, sizeof(TileElement));
                } while (!(element++)->isLastForTile());
            }
        }
        return hash;
    }

    SceneBuilder::SceneBuilder(ITextureProvider& textures)
        : _textures(textures)
    {
    }

    SceneBuilder::~SceneBuilder() = default;

    void SceneBuilder::FinishMesh(MeshData& out)
    {
        for (const auto& v : out.solid)
            out.Extend(v.position);
        for (const auto& v : out.transparent)
            out.Extend(v.position);
        _mesh = nullptr;
    }

    void SceneBuilder::BuildChunk(ChunkCoord chunk, MeshData& out)
    {
        out.Clear();
        _mesh = &out;

        const auto& mapSize = getGameState().mapSize;
        const int32_t x0 = chunk.x * kChunkSize;
        const int32_t y0 = chunk.y * kChunkSize;
        const int32_t x1 = std::min<int32_t>(mapSize.x, x0 + kChunkSize);
        const int32_t y1 = std::min<int32_t>(mapSize.y, y0 + kChunkSize);
        for (int32_t y = y0; y < y1; y++)
        {
            for (int32_t x = x0; x < x1; x++)
            {
                BuildTile({ x, y });
            }
        }

        FinishMesh(out);
    }

    void SceneBuilder::BuildChunkSprites(ChunkCoord chunk, uint8_t rotation, MeshData& out)
    {
        out.Clear();
        _mesh = &out;

        const auto& mapSize = getGameState().mapSize;
        const int32_t x0 = chunk.x * kChunkSize;
        const int32_t y0 = chunk.y * kChunkSize;
        const int32_t x1 = std::min<int32_t>(mapSize.x, x0 + kChunkSize);
        const int32_t y1 = std::min<int32_t>(mapSize.y, y0 + kChunkSize);

        PaintCapture capture(rotation);
        for (int32_t y = y0; y < y1; y++)
        {
            for (int32_t x = x0; x < x1; x++)
            {
                capture.AddTile({ x, y });
            }
        }
        _sprites.clear();
        capture.Collect(_sprites);
        AddCapturedSprites(rotation);

        FinishMesh(out);
    }

    void SceneBuilder::BuildEntities(const std::vector<TileCoordsXY>& tiles, uint8_t rotation, MeshData& out)
    {
        out.Clear();
        _mesh = &out;

        PaintCapture capture(rotation);
        for (const auto& tile : tiles)
        {
            capture.AddEntities(tile);
            AddVehicles(tile);
        }
        _sprites.clear();
        capture.Collect(_sprites);
        AddCapturedSprites(rotation);

        FinishMesh(out);
    }

    Colour4 SceneBuilder::GetGameColour(uint8_t colour)
    {
        const auto shade = Drawing::getColourMap(static_cast<Drawing::Colour>(colour)).light;
        return _textures.GetPaletteColour(EnumValue(shade));
    }

    void SceneBuilder::BuildTrack(const TileCoordsXY& tile, const TrackElement& track)
    {
        // A piece's whole path is built once, from its first element.
        if (track.getSequenceIndex() != 0 || !IsTrackElementReconstructed(track))
            return;

        const auto* ride = GetRide(track.getRideIndex());
        if (ride == nullptr)
            return;

        const auto style = GetTrackRideStyle(*ride);
        const CoordsXYZ origin{ tile.toCoordsXY(), track.getBaseZ() };
        if (!ComputeTrackFrames(
                VehicleTrackSubposition::standard, track.getTrackType(), track.getDirection(), origin, _frames))
            return;

        const auto& scheme = ride->trackColours[track.getColourScheme() % kNumRideColourSchemes];
        TrackColours colours;
        colours.main = GetGameColour(EnumValue(scheme.main));
        colours.additional = GetGameColour(EnumValue(scheme.additional));
        colours.supports = GetGameColour(EnumValue(scheme.supports));
        AddTrackMesh(_mesh->solid, _frames, style, colours);
    }

    void SceneBuilder::AddVehicles(const TileCoordsXY& tile)
    {
        for (auto* entity : EntityTileList(tile.toCoordsXY()))
        {
            const auto* vehicle = entity->as<Vehicle>();
            if (vehicle == nullptr)
                continue;
            const auto* ride = GetRide(vehicle->ride);
            if (ride == nullptr)
                continue;
            const auto style = GetTrackRideStyle(*ride);
            if (style.style == TrackMeshStyle::none)
                continue;

            TrackFrame frame;
            if (!GetVehicleFrame(*vehicle, style, frame, _frames))
                continue;

            VehicleColours colours;
            colours.body = GetGameColour(EnumValue(vehicle->colours.Body));
            colours.trim = GetGameColour(EnumValue(vehicle->colours.Trim));
            colours.riders = { 230, 180, 140, 255 };
            AddVehicleMesh(_mesh->solid, frame, style, colours, vehicle->num_peeps);
        }
    }

    // Whether a sprite is replaced by the 3D track or vehicle geometry.
    static bool IsReplacedByGeometry(const CapturedSprite& sprite)
    {
        if (sprite.entity != nullptr)
        {
            const auto* vehicle = sprite.entity->as<Vehicle>();
            if (vehicle == nullptr)
                return false;
            const auto* ride = GetRide(vehicle->ride);
            return ride != nullptr && GetTrackRideStyle(*ride).style != TrackMeshStyle::none;
        }
        if (sprite.element == nullptr || sprite.element->getType() != TileElementType::track)
            return false;
        // Keep the supports: they are drawn from below the track element, the track itself from its base upwards.
        if (sprite.boundsMin.z < sprite.element->getBaseZ() - 2)
            return false;
        return IsTrackElementReconstructed(*sprite.element->asTrack());
    }

    void SceneBuilder::AddCapturedSprites(uint8_t rotation)
    {
        MergeDuplicateSprites(_sprites);

        // The game often draws several layers with the same bounding box (a ride and its colour overlay, a building and
        // its roof, a parent and its attached images). Their faces land on exactly the same planes, which would flicker
        // as the depth test picks a different layer from pixel to pixel. Nudge each later layer slightly towards the
        // isometric camera, so the one the game draws on top also wins in 3D.
        float towardsX, towardsY;
        RotateXY(static_cast<uint8_t>((4 - rotation) & 3), 1.0f, 1.0f, towardsX, towardsY);
        const Vec3 towardsCamera = Normalize(Vec3{ towardsX, towardsY, 1.0f });
        constexpr float kLayerStep = 0.12f;

        std::unordered_map<uint64_t, uint32_t> layersPerBox;
        layersPerBox.reserve(_sprites.size());
        for (const auto& sprite : _sprites)
        {
            uint64_t boxKey = 14695981039346656037ull;
            for (const int32_t v : { sprite.boundsMin.x, sprite.boundsMin.y, sprite.boundsMin.z, sprite.boundsMax.x,
                                     sprite.boundsMax.y, sprite.boundsMax.z })
            {
                boxKey = (boxKey ^ static_cast<uint32_t>(v)) * 1099511628211ull;
            }
            const uint32_t layer = layersPerBox[boxKey]++;

            const size_t first = _mesh->solid.size();
            AddCapturedSprite(sprite, rotation);
            if (layer == 0)
                continue;
            const Vec3 offset = towardsCamera * (kLayerStep * static_cast<float>(std::min<uint32_t>(layer, 16)));
            for (size_t i = first; i < _mesh->solid.size(); i++)
                _mesh->solid[i].position += offset;
        }
    }

    void MergeDuplicateSprites(std::vector<CapturedSprite>& sprites)
    {
        // Multi-tile objects (flat rides, large stalls) are often painted whole from every tile they cover, at the same
        // screen position but with that tile's bounding box. In 2D the copies cover each other exactly; in 3D each
        // would land at a different depth. Keep one copy, with the box of the whole object.
        std::unordered_map<uint64_t, size_t> firstIndex;
        firstIndex.reserve(sprites.size());
        std::vector<CapturedSprite> merged;
        merged.reserve(sprites.size());
        for (const auto& sprite : sprites)
        {
            // Entities are never painted twice; tile elements of different objects may share an image and position
            // only if they really overlap.
            if (sprite.entity != nullptr)
            {
                merged.push_back(sprite);
                continue;
            }
            const uint64_t key = GetImageKey(sprite.image)
                ^ (static_cast<uint64_t>(static_cast<uint32_t>(sprite.screenPos.x)) * 0x9E3779B97F4A7C15ull)
                ^ (static_cast<uint64_t>(static_cast<uint32_t>(sprite.screenPos.y)) * 0xC2B2AE3D27D4EB4Full);
            const auto it = firstIndex.find(key);
            if (it == firstIndex.end())
            {
                firstIndex.emplace(key, merged.size());
                merged.push_back(sprite);
                continue;
            }
            auto& kept = merged[it->second];
            kept.boundsMin = { std::min(kept.boundsMin.x, sprite.boundsMin.x), std::min(kept.boundsMin.y, sprite.boundsMin.y),
                               std::min(kept.boundsMin.z, sprite.boundsMin.z) };
            kept.boundsMax = { std::max(kept.boundsMax.x, sprite.boundsMax.x), std::max(kept.boundsMax.y, sprite.boundsMax.y),
                               std::max(kept.boundsMax.z, sprite.boundsMax.z) };
        }
        sprites = std::move(merged);
    }

    void SceneBuilder::AddCapturedSprite(const CapturedSprite& sprite, uint8_t rotation)
    {
        // Terrain tops and water have real geometry already. The terrain's cliff faces are kept: their sprites sit in
        // thin vertical boxes on the tile edges and give the cliffs the game's rock and soil textures.
        if (sprite.interaction == ViewportInteractionItem::water)
            return;
        if (sprite.interaction == ViewportInteractionItem::terrain
            && sprite.boundsMax.z - sprite.boundsMin.z <= kFlatSpriteMaxHeight)
            return;
        if (sprite.element != nullptr && sprite.element->isGhost())
            return;
        // Blended images (glass, ghosts) tint whatever is behind them, which a texture cannot express.
        if (!sprite.image.HasValue() || sprite.image.IsBlended())
            return;
        // Scrolling banner text reuses the same image slots for changing content, which the texture cache cannot
        // follow.
        if (sprite.image.GetIndex() >= SPR_SCROLLING_TEXT_START && sprite.image.GetIndex() < SPR_SCROLLING_TEXT_END)
            return;
        if (IsReplacedByGeometry(sprite))
            return;

        const auto* g1 = GfxGetG1Element(sprite.image);
        if (g1 == nullptr || g1->width <= 0 || g1->height <= 0)
            return;

        const auto texture = _textures.GetSprite(sprite.image);
        if (!texture.IsValid())
            return;

        SpritePlacement placement;
        placement.rotation = rotation;
        placement.left = static_cast<float>(sprite.screenPos.x + g1->xOffset);
        placement.top = static_cast<float>(sprite.screenPos.y + g1->yOffset);
        placement.width = static_cast<float>(g1->width);
        placement.height = static_cast<float>(g1->height);
        placement.boundsMin = { static_cast<float>(sprite.boundsMin.x), static_cast<float>(sprite.boundsMin.y),
                                static_cast<float>(sprite.boundsMin.z) };
        placement.boundsMax = { static_cast<float>(sprite.boundsMax.x), static_cast<float>(sprite.boundsMax.y),
                                static_cast<float>(sprite.boundsMax.z) };

        auto& out = _mesh->solid;
        if (placement.boundsMax.z - placement.boundsMin.z > kFlatSpriteMaxHeight)
        {
            if (IsSpriteBoxMismatched(placement))
            {
                // Typically tall rides painted in stacked segments, each with a box away from the shared sprite.
                LOG_VERBOSE(
                    "3D view: sprite %u does not match its box (%d %d %d)-(%d %d %d), drawing it on a card",
                    sprite.image.GetIndex(), sprite.boundsMin.x, sprite.boundsMin.y, sprite.boundsMin.z, sprite.boundsMax.x,
                    sprite.boundsMax.y, sprite.boundsMax.z);
                AddSpriteCard(out, placement, texture);
                return;
            }
            AddSpriteBox(out, placement, texture);
            return;
        }

        // Lies on the ground: lift it slightly so it never fights with the terrain underneath.
        GroundPlane plane{ placement.boundsMax.z + 0.5f, 0.0f, 0.0f };
        if (sprite.interaction == ViewportInteractionItem::footpath && sprite.element != nullptr
            && sprite.element->getType() == TileElementType::path)
        {
            const auto* path = sprite.element->asPath();
            if (path->isSloped())
            {
                // Sloped paths rise one height step (16 units) across the tile towards the slope direction.
                const auto delta = CoordsDirectionDelta[path->getSlopeDirection()];
                plane.gradientX = static_cast<float>(delta.x) / kCoordsXYStep * 0.5f;
                plane.gradientY = static_cast<float>(delta.y) / kCoordsXYStep * 0.5f;
                const float centreX = static_cast<float>(sprite.mapPos.x + kCoordsXYHalfTile);
                const float centreY = static_cast<float>(sprite.mapPos.y + kCoordsXYHalfTile);
                plane.baseZ = static_cast<float>(path->getBaseZ()) + 8.5f - plane.gradientX * centreX
                    - plane.gradientY * centreY;
            }
        }
        AddSpriteOnPlane(out, placement, plane, texture);
    }

    void SceneBuilder::BuildTile(const TileCoordsXY& tile)
    {
        const TileElement* element = MapGetFirstElementAt(tile);
        if (element == nullptr)
            return;

        do
        {
            if (element->isGhost() || element->isInvisible())
                continue;

            switch (element->getType())
            {
                case TileElementType::surface:
                    BuildSurface(tile, *element->asSurface());
                    break;
                case TileElementType::track:
                    BuildTrack(tile, *element->asTrack());
                    break;
                default:
                    break;
            }
        } while (!(element++)->isLastForTile());
    }

    void SceneBuilder::BuildSurface(const TileCoordsXY& tile, const SurfaceElement& surface)
    {
        const auto corners = GetSlopeCornerHeights(surface.getBaseZ(), surface.getSlope());
        const float ox = static_cast<float>(tile.x * kCoordsXYStep);
        const float oy = static_cast<float>(tile.y * kCoordsXYStep);
        const float size = static_cast<float>(kCoordsXYStep);

        const Vec3 s = GameToWorld(ox, oy, static_cast<float>(corners.south));
        const Vec3 e = GameToWorld(ox + size, oy, static_cast<float>(corners.east));
        const Vec3 n = GameToWorld(ox + size, oy + size, static_cast<float>(corners.north));
        const Vec3 w = GameToWorld(ox, oy + size, static_cast<float>(corners.west));

        TextureRegion texture{};
        Colour4 colour{ 255, 255, 255, 255 };
        if (const auto* surfaceObject = surface.getSurfaceObject(); surfaceObject != nullptr)
        {
            const auto image = surfaceObject->GetImageId(
                tile.toCoordsXY(), surface.getGrassLength() & 0x7, 0, 0, false, false, surface.getPrimarySurfaceColour());
            texture = _textures.GetGroundProjection(image, {});
        }
        if (!texture.IsValid())
            colour = kFallbackTerrainColour;

        auto& out = _mesh->solid;
        // UVs: u along world x, v along world y. S = (0,0), E = (1,0), N = (1,1), W = (0,1).
        if (SplitAlongEastWest(corners))
        {
            AddTriangle(out, s, e, w, colour, &texture, { 0, 0, 1, 0, 0, 1 });
            AddTriangle(out, e, n, w, colour, &texture, { 1, 0, 1, 1, 0, 1 });
        }
        else
        {
            AddTriangle(out, s, e, n, colour, &texture, { 0, 0, 1, 0, 1, 1 });
            AddTriangle(out, s, n, w, colour, &texture, { 0, 0, 1, 1, 0, 1 });
        }

        BuildSurfaceEdges(tile, surface, corners);
        BuildWater(tile, surface, corners);
    }

    void SceneBuilder::BuildSurfaceEdges(const TileCoordsXY& tile, const SurfaceElement& surface, const TileCornersZ& corners)
    {
        Colour4 colour = kFallbackCliffColour;
        if (const auto* edgeObject = surface.getEdgeObject(); edgeObject != nullptr)
        {
            const auto region = _textures.GetSprite(ImageId(edgeObject->BaseImageId, surface.getPrimaryEdgeColour()));
            if (region.IsValid())
                colour = region.average;
        }

        const float ox = static_cast<float>(tile.x * kCoordsXYStep);
        const float oy = static_cast<float>(tile.y * kCoordsXYStep);
        const float size = static_cast<float>(kCoordsXYStep);

        const auto* neighbourXp = GetSurface({ tile.x + 1, tile.y });
        const auto* neighbourXn = GetSurface({ tile.x - 1, tile.y });
        const auto* neighbourYp = GetSurface({ tile.x, tile.y + 1 });
        const auto* neighbourYn = GetSurface({ tile.x, tile.y - 1 });

        auto neighbourCorners = [](const SurfaceElement* el) -> std::optional<TileCornersZ> {
            if (el == nullptr)
                return std::nullopt;
            return GetSlopeCornerHeights(el->getBaseZ(), el->getSlope());
        };

        const auto cXp = neighbourCorners(neighbourXp);
        const auto cXn = neighbourCorners(neighbourXn);
        const auto cYp = neighbourCorners(neighbourYp);
        const auto cYn = neighbourCorners(neighbourYn);

        auto emit = [&](float ax, float ay, float bx, float by, int32_t ownA, int32_t ownB, int32_t nbA, int32_t nbB,
                        const Vec3& outward, float shade) {
            const int32_t bottomA = std::min(ownA, nbA);
            const int32_t bottomB = std::min(ownB, nbB);
            if (bottomA >= ownA && bottomB >= ownB)
                return;

            // Set back behind the textured cliff sprites (whose boxes sit up to 2 units inside the tile edge) so they
            // win the depth test; this plain face is what remains visible beyond the sprite draw distance.
            const Vec3 inset = outward * -2.5f;
            const Vec3 a0 = GameToWorld(ox + ax * size, oy + ay * size, static_cast<float>(bottomA)) + inset;
            const Vec3 b0 = GameToWorld(ox + bx * size, oy + by * size, static_cast<float>(bottomB)) + inset;
            const Vec3 b1 = GameToWorld(ox + bx * size, oy + by * size, static_cast<float>(ownB)) + inset;
            const Vec3 a1 = GameToWorld(ox + ax * size, oy + ay * size, static_cast<float>(ownA)) + inset;

            // Wind the quad so that it faces away from the tile.
            const Vec3 n = Cross(b0 - a0, a1 - a0);
            if (Dot(n, outward) >= 0)
                AddQuad(_mesh->solid, a0, b0, b1, a1, Shade(colour, shade));
            else
                AddQuad(_mesh->solid, b0, a0, a1, b1, Shade(colour, shade));
        };

        // +x edge: own east/north against the neighbour's south/west.
        emit(
            1, 0, 1, 1, corners.east, corners.north, cXp ? cXp->south : 0, cXp ? cXp->west : 0, { 1, 0, 0 }, 0.8f);
        // -x edge: own south/west against the neighbour's east/north.
        emit(
            0, 0, 0, 1, corners.south, corners.west, cXn ? cXn->east : 0, cXn ? cXn->north : 0, { -1, 0, 0 }, 0.8f);
        // +y edge: own west/north against the neighbour's south/east.
        emit(
            0, 1, 1, 1, corners.west, corners.north, cYp ? cYp->south : 0, cYp ? cYp->east : 0, { 0, 1, 0 }, 0.9f);
        // -y edge: own south/east against the neighbour's west/north.
        emit(
            0, 0, 1, 0, corners.south, corners.east, cYn ? cYn->west : 0, cYn ? cYn->north : 0, { 0, -1, 0 }, 0.9f);
    }

    void SceneBuilder::BuildWater(const TileCoordsXY& tile, const SurfaceElement& surface, const TileCornersZ& corners)
    {
        const int32_t waterZ = surface.getWaterHeight();
        if (waterZ <= 0)
            return;

        const int32_t lowest = std::min({ corners.north, corners.east, corners.south, corners.west });
        if (waterZ <= lowest)
            return;

        const float ox = static_cast<float>(tile.x * kCoordsXYStep);
        const float oy = static_cast<float>(tile.y * kCoordsXYStep);
        const float size = static_cast<float>(kCoordsXYStep);
        const float z = static_cast<float>(waterZ);
        AddQuad(
            _mesh->transparent, GameToWorld(ox, oy, z), GameToWorld(ox + size, oy, z), GameToWorld(ox + size, oy + size, z),
            GameToWorld(ox, oy + size, z), kWaterColour);
    }
} // namespace OpenRCT2::Render3D
