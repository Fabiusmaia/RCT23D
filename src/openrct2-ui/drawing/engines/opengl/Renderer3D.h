/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "OpenGLAPI.h"
#include "OpenGLFramebuffer.h"
#include "OpenGLShaderProgram.h"

#include <SDL_pixels.h>
#include <chrono>
#include <memory>
#include <string>
#include <openrct2/render3d/SceneBuilder.h>
#include <openrct2/render3d/SceneTypes.h>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Render3D
{
    struct IndexedImage;
}

namespace OpenRCT2::Ui
{
    // Packs re-coloured game sprites into an RGBA texture array for the 3D renderer.
    class TextureAtlas3D final : public Render3D::ITextureProvider
    {
    private:
        static constexpr int32_t kLayerSize = 1024;
        // Large enough for a big park, so the atlas rarely has to grow (which rebuilds every mesh).
        static constexpr int32_t kInitialLayers = 32;
        static constexpr int32_t kMaxLayers = 96;
        static constexpr int32_t kGutter = 8;
        // Mip levels below full size; the gutter is wide enough that none of them samples a neighbouring image.
        static constexpr int32_t kMipLevels = 3;
        static constexpr int32_t kGroundResolution = 64;

        struct Shelf
        {
            int32_t y{};
            int32_t height{};
            int32_t x{};
        };
        struct LayerState
        {
            std::vector<Shelf> shelves;
            int32_t nextY{};
        };

        const SDL_Color* _palette;
        GLuint _texture = 0;
        int32_t _layerCount = 0;
        std::vector<LayerState> _layers;
        std::unordered_map<uint64_t, Render3D::TextureRegion> _cache;
        bool _overflowed = false;
        bool _full = false;
        std::chrono::steady_clock::time_point _lastRecycle{};
        // Sprites are doubled with Scale2x and magnified with linear filtering.
        bool _smooth = false;
        // Diagnostics: how often the atlas was rebuilt from scratch.
        uint32_t _allocations = 0;

    public:
        explicit TextureAtlas3D(const SDL_Color* palette);
        ~TextureAtlas3D() override;

        TextureAtlas3D(const TextureAtlas3D&) = delete;
        TextureAtlas3D& operator=(const TextureAtlas3D&) = delete;

        Render3D::TextureRegion GetSprite(ImageId image) override;
        Render3D::TextureRegion GetGroundProjection(ImageId image, const Render3D::TileCornerOffsets& corners) override;
        Render3D::Colour4 GetPaletteColour(uint8_t paletteIndex) override;

        GLuint GetTexture() const
        {
            return _texture;
        }

        // True once after the atlas ran out of space and was reset; every mesh using it must be rebuilt.
        bool ConsumeOverflow();

        // Forgets every image, e.g. after objects were loaded or unloaded and image indices changed meaning.
        void Clear();

        // Switches smoothing on or off. Returns true if it changed, in which case every mesh must be rebuilt.
        bool SetSmooth(bool smooth);

        void FinishUploads();

        uint32_t GetAllocationCount() const
        {
            return _allocations;
        }
        int32_t GetLayerCount() const
        {
            return _layerCount;
        }

    private:
        void Allocate(int32_t layers);
        bool Reserve(int32_t width, int32_t height, int32_t& outX, int32_t& outY, int32_t& outLayer);
        Render3D::TextureRegion Upload(const Render3D::IndexedImage& image, bool opaque);
    };

    // One vertex buffer with its vertex array.
    struct GpuMesh
    {
        GLuint vao{};
        GLuint vbo{};
        GLsizei count{};
        size_t capacity{};
    };

    class Renderer3D
    {
    private:
        struct ChunkMesh
        {
            // Terrain (solid) and water (transparent).
            GpuMesh terrain;
            GpuMesh water;
            Render3D::AABB terrainBounds{};
            bool built{};
            bool dirty = true;
            uint64_t hash{};

            // Sprite geometry for each isometric view rotation, built when first needed.
            GpuMesh sprites[4];
            Render3D::AABB spriteBounds[4]{};
            bool spritesBuilt[4]{};

            // Terrain and sprites of the current rotation together, used for culling.
            Render3D::AABB bounds{};
            // Which sprite rotation is drawn this frame (-1 for none).
            int32_t drawRotation = -1;
        };

        struct Locations
        {
            GLint viewProj = -1;
            GLint cameraRight = -1;
            GLint rotation = -1;
            GLint texture = -1;
            GLint sunDirection = -1;
            GLint sunColour = -1;
            GLint ambientColour = -1;
            GLint fogColour = -1;
            GLint fogStart = -1;
            GLint fogEnd = -1;
            GLint cameraPos = -1;
            GLint transparentPass = -1;
            GLint time = -1;
            GLint position = -1;
            GLint normal = -1;
            GLint uv = -1;
            GLint layer = -1;
            GLint colour = -1;
            GLint offset = -1;
            GLint vertexRotation = -1;
            GLint flags = -1;
            GLint lightViewProj = -1;
            GLint shadowMap = -1;
            GLint shadowsEnabled = -1;
            GLint shadowTexel = -1;
            GLint shadowPass = -1;
            GLint skyColour = -1;
            GLint ditherMode = -1;
            GLint fade = -1;
        };

        struct SkyLocations
        {
            GLint invViewProj = -1;
            GLint zenithColour = -1;
            GLint horizonColour = -1;
            GLint sunDirection = -1;
            GLint sunColour = -1;
            GLint sunVisible = -1;
            GLint position = -1;
        };

        std::unique_ptr<OpenGLShaderProgram> _program;
        Locations _loc;

        std::unique_ptr<OpenGLShaderProgram> _skyProgram;
        SkyLocations _skyLoc;
        GLuint _skyVao = 0;
        GLuint _skyVbo = 0;

        // Sun shadow map: depth seen from the sun over the area around the camera.
        static constexpr int32_t kShadowMapSize = 2048;
        GLuint _shadowFbo = 0;
        GLuint _shadowTexture = 0;
        bool _shadowsUnavailable = false;
        std::unique_ptr<TextureAtlas3D> _atlas;
        std::unique_ptr<OpenGLFramebuffer> _framebuffer;

        std::vector<ChunkMesh> _chunks;
        int32_t _chunkCountX = 0;
        int32_t _chunkCountY = 0;
        size_t _hashCursor = 0;
        float _time = 0;
        bool _imagesChanged = false;

        // Sprite rotation in use, with hysteresis, and the one being faded out after a switch (-1 for none).
        uint8_t _isoRotation = 0;
        bool _hasIsoRotation = false;
        int32_t _fadeFrom = -1;
        float _fade = 1.0f;
        float _lastFrameSeconds = 0.0f;
        uint32_t _imageInvalidations = 0;

        // Guests, staff, vehicles, ... rebuilt every frame from the simulation.
        GpuMesh _entities;
        std::vector<TileCoordsXY> _entityTiles;

        Render3D::MeshData _scratch;

        struct FrameStats
        {
            size_t chunksBuilt{};
            size_t spriteChunksBuilt{};
            size_t visibleChunks{};
            size_t vertices{};
            size_t entityVertices{};
            float buildMs{};
            float entityMs{};
            float renderMs{};
        };
        FrameStats _stats;
        // Smoothed time between frames, which includes waiting for the GPU.
        float _frameMs{};
        std::chrono::steady_clock::time_point _lastFrame{};

    public:
        explicit Renderer3D(const SDL_Color* palette);
        ~Renderer3D();

        Renderer3D(const Renderer3D&) = delete;
        Renderer3D& operator=(const Renderer3D&) = delete;

        // Draws the 3D view into the internal framebuffer. The viewport rectangle uses screen coordinates (top-left origin).
        void Render(int32_t screenWidth, int32_t screenHeight, int32_t vpX, int32_t vpY, int32_t vpWidth, int32_t vpHeight);

        OpenGLFramebuffer& GetFramebuffer()
        {
            return *_framebuffer;
        }

        // A one-line summary of the last frame, for the console.
        std::string GetStatsText() const;

        // Game images were replaced (objects loaded or unloaded); all textures and meshes are rebuilt next frame.
        void InvalidateImages()
        {
            _imagesChanged = true;
            _imageInvalidations++;
        }

    private:
        void GetLocations();
        bool EnsureShadowMap();
        void FreeShadowMap();
        // Renders the depth of everything near the camera as seen from the sun. Returns the light's view-projection.
        Render3D::Mat4 RenderShadowMap(
            const Render3D::Vec3& cameraPos, const Render3D::Vec3& forward, const Render3D::Vec3& sunDirection,
            uint8_t rotation, float radius);
        void DrawSky(
            const Render3D::Mat4& viewProj, const Render3D::Vec3& zenith, const Render3D::Vec3& horizon,
            const Render3D::Vec3& sunDirection, const Render3D::Vec3& sunColour, float sunVisible);
        void ResetChunks();
        void FreeChunk(ChunkMesh& chunk);
        void FreeMesh(GpuMesh& mesh);
        void UploadMesh(GpuMesh& mesh, const std::vector<Render3D::Vertex>& vertices, GLenum usage);
        void SetupVertexArray(GLuint vao, GLuint vbo);
        void DrawMesh(const GpuMesh& mesh);
        void InvalidateAllChunks();
        static int32_t PickSpriteRotation(const ChunkMesh& chunk, uint8_t wanted);
        void CheckChunkHashes();
        void BuildVisibleChunks(
            const Render3D::Frustum& frustum, const Render3D::Vec3& cameraPos, uint8_t rotation, float drawDistance,
            float spriteDistance);
        void BuildEntities(const Render3D::Frustum& frustum, const Render3D::Vec3& cameraPos, uint8_t rotation, float distance);
    };
} // namespace OpenRCT2::Ui
