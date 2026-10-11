/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../world/Location.hpp"
#include "SceneTypes.h"

#include <cstdint>

namespace OpenRCT2
{
    struct SurfaceElement;
    struct TileElement;
    struct TrackElement;
} // namespace OpenRCT2

namespace OpenRCT2::Render3D
{
    struct ChunkCoord
    {
        int32_t x{};
        int32_t y{};

        bool operator==(const ChunkCoord& other) const = default;
    };

    // Splits the world's tile triangulation the same way for every consumer (rendering, collision and tests).
    // Returns true when the tile should be split along the east-west diagonal, false for north-south.
    bool SplitAlongEastWest(const TileCornersZ& corners);

    // Emits geometry helpers. Exposed for unit tests.
    void AddQuad(
        std::vector<Vertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, Colour4 colour,
        const TextureRegion* texture = nullptr, const std::array<float, 8>* uvs = nullptr);
    void AddTriangle(
        std::vector<Vertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, Colour4 colour, const TextureRegion* texture,
        const std::array<float, 6>& uvs);
    void AddBox(std::vector<Vertex>& out, const AABB& box, Colour4 colour);

    struct CapturedSprite;
    struct TrackFrame;

    // Collapses sprites that multi-tile objects paint identically from each of their tiles into one, whose bounding box
    // covers the whole object. Exposed for unit tests.
    void MergeDuplicateSprites(std::vector<CapturedSprite>& sprites);

    // Builds the static geometry (everything stored in tile elements) for one chunk of the map.
    class SceneBuilder
    {
    private:
        ITextureProvider& _textures;
        MeshData* _mesh = nullptr;
        std::vector<CapturedSprite> _sprites;
        std::vector<TrackFrame> _frames;

    public:
        explicit SceneBuilder(ITextureProvider& textures);
        ~SceneBuilder();

        // Terrain and water, which look the same from every direction.
        void BuildChunk(ChunkCoord chunk, MeshData& out);

        // Everything else stored in the chunk's tile elements (paths, scenery, walls, rides, ...), built from the sprites
        // the game draws for the given isometric view rotation.
        void BuildChunkSprites(ChunkCoord chunk, uint8_t rotation, MeshData& out);

        // Guests, staff, vehicles and other entities on the given tiles, as seen from the given view rotation.
        void BuildEntities(const std::vector<TileCoordsXY>& tiles, uint8_t rotation, MeshData& out);

        // Hash of every tile element that can affect the chunk's geometry, including a one-tile border.
        static uint64_t ComputeChunkHash(ChunkCoord chunk);

        static int32_t GetChunkCountX();
        static int32_t GetChunkCountY();

    private:
        void BuildTile(const TileCoordsXY& tile);
        void BuildSurface(const TileCoordsXY& tile, const SurfaceElement& surface);
        void BuildSurfaceEdges(const TileCoordsXY& tile, const SurfaceElement& surface, const TileCornersZ& corners);
        void BuildWater(const TileCoordsXY& tile, const SurfaceElement& surface, const TileCornersZ& corners);
        void BuildTrack(const TileCoordsXY& tile, const TrackElement& track);
        void AddVehicles(const TileCoordsXY& tile);
        void AddCapturedSprites(uint8_t rotation);
        void AddCapturedSprite(const CapturedSprite& sprite, uint8_t rotation);
        void FinishMesh(MeshData& out);
        Colour4 GetGameColour(uint8_t colour);
    };
} // namespace OpenRCT2::Render3D
