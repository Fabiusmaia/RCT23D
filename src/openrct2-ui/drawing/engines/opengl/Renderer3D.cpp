/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef DISABLE_OPENGL

    #include "Renderer3D.h"

    #include <algorithm>
    #include <chrono>
    #include <cstddef>
    #include <cstdio>
    #include <openrct2/Diagnostic.h>
    #include <openrct2/Game.h>
    #include <openrct2/GameState.h>
    #include <openrct2/render3d/Render3D.h>
    #include <openrct2/render3d/SpriteBaker.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Render3D;
using namespace OpenRCT2::Ui;

namespace
{
    constexpr uint64_t kSpriteKeyTag = 1ull << 62;
    constexpr uint64_t kGroundKeyTag = 1ull << 63;

    Vec3 Mix(const Vec3& a, const Vec3& b, float t)
    {
        return a + (b - a) * t;
    }

    void SetUniform(GLint location, const Vec3& v)
    {
        if (location >= 0)
            glCall(glUniform3f, location, v.x, v.y, v.z);
    }
} // namespace

    #pragma region TextureAtlas3D

TextureAtlas3D::TextureAtlas3D(const SDL_Color* palette)
    : _palette(palette)
{
    Allocate(kInitialLayers);
}

TextureAtlas3D::~TextureAtlas3D()
{
    if (_texture != 0)
        glCall(glDeleteTextures, 1, &_texture);
}

void TextureAtlas3D::Allocate(int32_t layers)
{
    if (_texture != 0)
        glCall(glDeleteTextures, 1, &_texture);

    _allocations++;
    _layerCount = layers;
    _layers.assign(layers, {});
    _cache.clear();

    glCall(glGenTextures, 1, &_texture);
    OpenGLAPI::SetTexture(0, GL_TEXTURE_2D_ARRAY, _texture);
    // Mip levels are allocated up front and filled per image as it is uploaded (see Upload).
    for (int32_t level = 0; level <= kMipLevels; level++)
    {
        glCall(
            glTexImage3D, GL_TEXTURE_2D_ARRAY, level, GL_RGBA8, kLayerSize >> level, kLayerSize >> level, layers, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, _smooth ? GL_LINEAR : GL_NEAREST);
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    // The gutter around each image is 8 pixels wide, so 3 mip levels never sample a neighbour.
    glCall(glTexParameteri, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, kMipLevels);
}

void TextureAtlas3D::Clear()
{
    Allocate(_layerCount);
    _full = false;
    _overflowed = false;
}

bool TextureAtlas3D::SetSmooth(bool smooth)
{
    if (smooth == _smooth)
        return false;
    _smooth = smooth;
    // Doubled images need more room; start over at a size that fits them.
    Allocate(_layerCount);
    _full = false;
    return true;
}

bool TextureAtlas3D::ConsumeOverflow()
{
    const bool result = _overflowed;
    _overflowed = false;
    return result;
}

bool TextureAtlas3D::Reserve(int32_t width, int32_t height, int32_t& outX, int32_t& outY, int32_t& outLayer)
{
    if (width > kLayerSize || height > kLayerSize)
        return false;

    for (int32_t layer = 0; layer < _layerCount; layer++)
    {
        auto& state = _layers[layer];
        for (auto& shelf : state.shelves)
        {
            if (height <= shelf.height && height * 2 >= shelf.height && shelf.x + width <= kLayerSize)
            {
                outX = shelf.x;
                outY = shelf.y;
                outLayer = layer;
                shelf.x += width;
                return true;
            }
        }
        if (state.nextY + height <= kLayerSize)
        {
            state.shelves.push_back({ state.nextY, height, width });
            outX = 0;
            outY = state.nextY;
            outLayer = layer;
            state.nextY += height;
            return true;
        }
    }
    return false;
}

Render3D::TextureRegion TextureAtlas3D::Upload(const IndexedImage& image, bool opaque)
{
    // Sizes (and so positions) are multiples of 8, so every mip level of the image starts on a whole texel.
    constexpr int32_t kAlign = 1 << kMipLevels;
    const int32_t paddedWidth = (image.width + kGutter * 2 + kAlign - 1) / kAlign * kAlign;
    const int32_t paddedHeight = (image.height + kGutter * 2 + kAlign - 1) / kAlign * kAlign;

    int32_t x = 0, y = 0, layer = 0;
    if (!Reserve(paddedWidth, paddedHeight, x, y, layer))
    {
        if (paddedWidth > kLayerSize || paddedHeight > kLayerSize)
            return {};
        const auto now = std::chrono::steady_clock::now();
        if (_layerCount >= kMaxLayers)
        {
            // Full: images for views and places no longer on screen pile up (each isometric rotation has its own
            // sprites). Start over so only what is visible now gets uploaded again. If that already happened moments
            // ago, what is on screen alone does not fit; skip images rather than rebuild every frame.
            if (now - _lastRecycle < std::chrono::seconds(3))
            {
                if (!_full)
                    LOG_WARNING("3D texture atlas is full (%d layers); some objects are not drawn", _layerCount);
                _full = true;
                return {};
            }
            LOG_VERBOSE("3D texture atlas is full, recycling it");
            _lastRecycle = now;
            _overflowed = true;
            Allocate(_layerCount);
            _full = false;
        }
        else
        {
            // Out of space: grow and start over. Callers rebuild their meshes after ConsumeOverflow().
            _overflowed = true;
            Allocate(std::min(_layerCount * 2, kMaxLayers));
        }
        if (!Reserve(paddedWidth, paddedHeight, x, y, layer))
            return {};
    }

    // Average colour of the opaque pixels, used for transparent texels and untextured stand-ins.
    uint32_t sum[3] = {};
    uint32_t opaqueCount = 0;
    for (auto index : image.pixels)
    {
        if (index == 0)
            continue;
        const auto& c = _palette[index];
        sum[0] += c.r;
        sum[1] += c.g;
        sum[2] += c.b;
        opaqueCount++;
    }
    if (opaqueCount == 0)
        return {};

    const Colour4 average{ static_cast<uint8_t>(sum[0] / opaqueCount), static_cast<uint8_t>(sum[1] / opaqueCount),
                           static_cast<uint8_t>(sum[2] / opaqueCount), 255 };

    std::vector<uint8_t> rgba(static_cast<size_t>(paddedWidth) * paddedHeight * 4);
    for (int32_t py = 0; py < paddedHeight; py++)
    {
        for (int32_t px = 0; px < paddedWidth; px++)
        {
            int32_t sx = px - kGutter;
            int32_t sy = py - kGutter;
            uint8_t index;
            if (opaque)
            {
                // Extend edge pixels into the gutter so filtering at tile seams stays seamless.
                sx = std::clamp(sx, 0, image.width - 1);
                sy = std::clamp(sy, 0, image.height - 1);
                index = image.pixels[static_cast<size_t>(sy) * image.width + sx];
            }
            else
            {
                index = image.At(sx, sy);
            }

            auto* dst = &rgba[(static_cast<size_t>(py) * paddedWidth + px) * 4];
            if (index == 0)
            {
                dst[0] = average.r;
                dst[1] = average.g;
                dst[2] = average.b;
                dst[3] = opaque ? 255 : 0;
            }
            else
            {
                const auto& c = _palette[index];
                dst[0] = c.r;
                dst[1] = c.g;
                dst[2] = c.b;
                dst[3] = 255;
            }
        }
    }

    OpenGLAPI::SetTexture(0, GL_TEXTURE_2D_ARRAY, _texture);
    glCall(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
    glCall(
        glTexSubImage3D, GL_TEXTURE_2D_ARRAY, 0, x, y, layer, paddedWidth, paddedHeight, 1, GL_RGBA, GL_UNSIGNED_BYTE,
        rgba.data());

    // Smaller versions for distant objects, only for this image: regenerating the mip levels of the whole atlas on
    // every upload would cost far more than drawing the frame.
    int32_t levelWidth = paddedWidth;
    int32_t levelHeight = paddedHeight;
    std::vector<uint8_t> level = std::move(rgba);
    for (int32_t mip = 1; mip <= kMipLevels; mip++)
    {
        const int32_t w = levelWidth / 2;
        const int32_t h = levelHeight / 2;
        std::vector<uint8_t> smaller(static_cast<size_t>(w) * h * 4);
        for (int32_t py = 0; py < h; py++)
        {
            for (int32_t px = 0; px < w; px++)
            {
                // Average weighted by coverage, so transparent texels do not darken the edges.
                uint32_t colour[3] = {};
                uint32_t alpha = 0;
                uint32_t plain[3] = {};
                for (int32_t s = 0; s < 4; s++)
                {
                    const auto* src = &level[(static_cast<size_t>(py * 2 + s / 2) * levelWidth + px * 2 + s % 2) * 4];
                    for (int32_t c = 0; c < 3; c++)
                    {
                        colour[c] += src[c] * src[3];
                        plain[c] += src[c];
                    }
                    alpha += src[3];
                }
                auto* dst = &smaller[(static_cast<size_t>(py) * w + px) * 4];
                for (int32_t c = 0; c < 3; c++)
                    dst[c] = static_cast<uint8_t>(alpha > 0 ? colour[c] / alpha : plain[c] / 4);
                dst[3] = static_cast<uint8_t>(alpha / 4);
            }
        }
        glCall(
            glTexSubImage3D, GL_TEXTURE_2D_ARRAY, mip, x >> mip, y >> mip, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE,
            smaller.data());
        level = std::move(smaller);
        levelWidth = w;
        levelHeight = h;
    }

    Render3D::TextureRegion region;
    region.u0 = static_cast<float>(x + kGutter) / kLayerSize;
    region.v0 = static_cast<float>(y + kGutter) / kLayerSize;
    region.u1 = static_cast<float>(x + kGutter + image.width) / kLayerSize;
    region.v1 = static_cast<float>(y + kGutter + image.height) / kLayerSize;
    region.layer = static_cast<float>(layer);
    region.average = average;
    return region;
}

Render3D::TextureRegion TextureAtlas3D::GetSprite(ImageId image)
{
    const uint64_t key = GetImageKey(image) | kSpriteKeyTag;
    if (auto it = _cache.find(key); it != _cache.end())
        return it->second;

    Render3D::TextureRegion region{};
    IndexedImage sprite;
    if (RasterizeSprite(image, sprite))
        region = Upload(_smooth ? Scale2x(sprite) : sprite, false);

    // Cache failures too so missing images are not retried every frame.
    _cache[key] = region;
    return region;
}

Render3D::TextureRegion TextureAtlas3D::GetGroundProjection(ImageId image, const TileCornerOffsets& corners)
{
    uint64_t key = GetImageKey(image);
    uint64_t cornerHash = static_cast<uint16_t>(corners.north) | (static_cast<uint64_t>(static_cast<uint16_t>(corners.east)) << 16)
        | (static_cast<uint64_t>(static_cast<uint16_t>(corners.south)) << 32)
        | (static_cast<uint64_t>(static_cast<uint16_t>(corners.west)) << 48);
    key ^= cornerHash * 0x9E3779B97F4A7C15ull;
    key = (key & ~(3ull << 62)) | kGroundKeyTag;

    if (auto it = _cache.find(key); it != _cache.end())
        return it->second;

    Render3D::TextureRegion region{};
    IndexedImage sprite;
    if (RasterizeSprite(image, sprite))
    {
        if (_smooth)
        {
            auto projected = ProjectGroundSprite(Scale2x(sprite), corners, kGroundResolution * 2, 2);
            region = Upload(projected, true);
        }
        else
        {
            auto projected = ProjectGroundSprite(sprite, corners, kGroundResolution);
            region = Upload(projected, true);
        }
    }
    _cache[key] = region;
    return region;
}

Render3D::Colour4 TextureAtlas3D::GetPaletteColour(uint8_t paletteIndex)
{
    const auto& c = _palette[paletteIndex];
    return { c.r, c.g, c.b, 255 };
}

void TextureAtlas3D::FinishUploads()
{
    // Mip levels are written with each image in Upload; nothing is left to do per frame.
}

    #pragma endregion

    #pragma region Renderer3D

Renderer3D::Renderer3D(const SDL_Color* palette)
{
    _program = std::make_unique<OpenGLShaderProgram>("render3d");
    GetLocations();
    _atlas = std::make_unique<TextureAtlas3D>(palette);

    _skyProgram = std::make_unique<OpenGLShaderProgram>("render3d_sky");
    _skyLoc.invViewProj = _skyProgram->GetUniformLocation("uInvViewProj");
    _skyLoc.zenithColour = _skyProgram->GetUniformLocation("uZenithColour");
    _skyLoc.horizonColour = _skyProgram->GetUniformLocation("uHorizonColour");
    _skyLoc.sunDirection = _skyProgram->GetUniformLocation("uSunDirection");
    _skyLoc.sunColour = _skyProgram->GetUniformLocation("uSunColour");
    _skyLoc.sunVisible = _skyProgram->GetUniformLocation("uSunVisible");
    _skyLoc.position = _skyProgram->GetAttributeLocation("vPosition");

    // One triangle covering the whole viewport.
    static constexpr float kSkyTriangle[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    glCall(glGenVertexArrays, 1, &_skyVao);
    glCall(glGenBuffers, 1, &_skyVbo);
    glCall(glBindVertexArray, _skyVao);
    glCall(glBindBuffer, GL_ARRAY_BUFFER, _skyVbo);
    glCall(glBufferData, GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kSkyTriangle)), kSkyTriangle, GL_STATIC_DRAW);
    if (_skyLoc.position >= 0)
    {
        glCall(glEnableVertexAttribArray, static_cast<GLuint>(_skyLoc.position));
        glCall(glVertexAttribPointer, static_cast<GLuint>(_skyLoc.position), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glCall(glBindVertexArray, 0);
}

Renderer3D::~Renderer3D()
{
    for (auto& chunk : _chunks)
        FreeChunk(chunk);
    FreeMesh(_entities);
    FreeShadowMap();
    if (_skyVbo != 0)
        glCall(glDeleteBuffers, 1, &_skyVbo);
    if (_skyVao != 0)
        glCall(glDeleteVertexArrays, 1, &_skyVao);
}

bool Renderer3D::EnsureShadowMap()
{
    if (_shadowFbo != 0)
        return true;
    if (_shadowsUnavailable)
        return false;

    glCall(glGenTextures, 1, &_shadowTexture);
    glCall(glActiveTexture, GL_TEXTURE1);
    glCall(glBindTexture, GL_TEXTURE_2D, _shadowTexture);
    glCall(
        glTexImage2D, GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowMapSize, kShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
        nullptr);
    // Hardware depth comparison with bilinear filtering gives soft shadow edges.
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCall(glActiveTexture, GL_TEXTURE0);

    glCall(glGenFramebuffers, 1, &_shadowFbo);
    glCall(glBindFramebuffer, GL_FRAMEBUFFER, _shadowFbo);
    glCall(glFramebufferTexture2D, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowTexture, 0);
    glCall(glDrawBuffer, GL_NONE);
    glCall(glReadBuffer, GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_WARNING("3D view: shadow map framebuffer is incomplete (0x%04X); shadows are disabled", status);
        FreeShadowMap();
        _shadowsUnavailable = true;
        return false;
    }
    return true;
}

void Renderer3D::FreeShadowMap()
{
    if (_shadowFbo != 0)
        glCall(glDeleteFramebuffers, 1, &_shadowFbo);
    if (_shadowTexture != 0)
        glCall(glDeleteTextures, 1, &_shadowTexture);
    _shadowFbo = 0;
    _shadowTexture = 0;
}

Mat4 Renderer3D::RenderShadowMap(const Vec3& cameraPos, const Vec3& forward, const Vec3& sunDirection, uint8_t rotation, float radius)
{
    // Cover the area in front of the camera, where shadows are seen.
    Vec3 flatForward = Normalize(Vec3{ forward.x, forward.y, 0.0f });
    if (Length(flatForward) < 0.5f)
        flatForward = { 1, 0, 0 };
    const Vec3 centre = cameraPos + flatForward * (radius * 0.6f);

    // Light space centred on the map origin; the projection window moves, snapped to whole texels so shadow edges
    // do not shimmer as the camera moves.
    constexpr float kLightDistance = 20000.0f;
    const Mat4 lightView = LookAt(sunDirection * kLightDistance, { 0, 0, 0 }, { 0, 0, 1 });
    const auto c = lightView.Transform(centre);
    const float texel = (radius * 2.0f) / kShadowMapSize;
    const float cx = std::floor(c[0] / texel) * texel;
    const float cy = std::floor(c[1] / texel) * texel;
    const Mat4 lightProj = Orthographic(cx - radius, cx + radius, cy - radius, cy + radius, kLightDistance - 12000.0f,
                                        kLightDistance + 12000.0f);
    const Mat4 lightViewProj = lightProj * lightView;
    const Frustum lightFrustum(lightViewProj);

    glCall(glBindFramebuffer, GL_FRAMEBUFFER, _shadowFbo);
    glCall(glViewport, 0, 0, kShadowMapSize, kShadowMapSize);
    glCall(glClearDepth, 1.0);
    glCall(glClear, GL_DEPTH_BUFFER_BIT);
    glCall(glEnable, GL_DEPTH_TEST);
    glCall(glDepthFunc, GL_LESS);
    glCall(glDepthMask, GL_TRUE);
    glCall(glDisable, GL_BLEND);
    // Sprite cards are single sided; both sides must cast shadows.
    glCall(glDisable, GL_CULL_FACE);

    glCall(glUniformMatrix4fv, _loc.viewProj, 1, GL_FALSE, lightViewProj.m.data());
    glCall(glUniform1i, _loc.shadowPass, 1);
    glCall(glUniform1i, _loc.shadowsEnabled, 0);
    glCall(glUniform1i, _loc.transparentPass, 0);

    for (auto& chunk : _chunks)
    {
        if (!chunk.built)
            continue;
        AABB bounds = chunk.terrainBounds;
        const int32_t spriteRotation = PickSpriteRotation(chunk, rotation);
        if (spriteRotation >= 0 && chunk.sprites[spriteRotation].count > 0)
        {
            const auto& sb = chunk.spriteBounds[spriteRotation];
            bounds.min = { std::min(bounds.min.x, sb.min.x), std::min(bounds.min.y, sb.min.y), std::min(bounds.min.z, sb.min.z) };
            bounds.max = { std::max(bounds.max.x, sb.max.x), std::max(bounds.max.y, sb.max.y), std::max(bounds.max.z, sb.max.z) };
        }
        if (!lightFrustum.Intersects(bounds))
            continue;
        DrawMesh(chunk.terrain);
        if (spriteRotation >= 0)
            DrawMesh(chunk.sprites[spriteRotation]);
    }
    DrawMesh(_entities);

    glCall(glUniform1i, _loc.shadowPass, 0);
    return lightViewProj;
}

void Renderer3D::DrawSky(
    const Mat4& viewProj, const Vec3& zenith, const Vec3& horizon, const Vec3& sunDirection, const Vec3& sunColour,
    float sunVisible)
{
    _skyProgram->Use();
    const Mat4 inverse = Inverse(viewProj);
    glCall(glUniformMatrix4fv, _skyLoc.invViewProj, 1, GL_FALSE, inverse.m.data());
    SetUniform(_skyLoc.zenithColour, zenith);
    SetUniform(_skyLoc.horizonColour, horizon);
    SetUniform(_skyLoc.sunDirection, sunDirection);
    SetUniform(_skyLoc.sunColour, sunColour);
    glCall(glUniform1f, _skyLoc.sunVisible, sunVisible);

    glCall(glDisable, GL_DEPTH_TEST);
    glCall(glDepthMask, GL_FALSE);
    glCall(glDisable, GL_BLEND);
    glCall(glDisable, GL_CULL_FACE);
    glCall(glBindVertexArray, _skyVao);
    glCall(glDrawArrays, GL_TRIANGLES, 0, 3);
    glCall(glDepthMask, GL_TRUE);
}

void Renderer3D::GetLocations()
{
    _loc.viewProj = _program->GetUniformLocation("uViewProj");
    _loc.cameraRight = _program->GetUniformLocation("uCameraRight");
    _loc.rotation = _program->GetUniformLocation("uRotation");
    _loc.texture = _program->GetUniformLocation("uTexture");
    _loc.sunDirection = _program->GetUniformLocation("uSunDirection");
    _loc.sunColour = _program->GetUniformLocation("uSunColour");
    _loc.ambientColour = _program->GetUniformLocation("uAmbientColour");
    _loc.fogColour = _program->GetUniformLocation("uFogColour");
    _loc.fogStart = _program->GetUniformLocation("uFogStart");
    _loc.fogEnd = _program->GetUniformLocation("uFogEnd");
    _loc.cameraPos = _program->GetUniformLocation("uCameraPos");
    _loc.transparentPass = _program->GetUniformLocation("uTransparentPass");
    _loc.time = _program->GetUniformLocation("uTime");

    _loc.position = _program->GetAttributeLocation("vPosition");
    _loc.normal = _program->GetAttributeLocation("vNormal");
    _loc.uv = _program->GetAttributeLocation("vUV");
    _loc.layer = _program->GetAttributeLocation("vLayer");
    _loc.colour = _program->GetAttributeLocation("vColour");
    _loc.offset = _program->GetAttributeLocation("vOffset");
    _loc.vertexRotation = _program->GetAttributeLocation("vRotation");
    _loc.flags = _program->GetAttributeLocation("vFlags");
    _loc.lightViewProj = _program->GetUniformLocation("uLightViewProj");
    _loc.shadowMap = _program->GetUniformLocation("uShadowMap");
    _loc.shadowsEnabled = _program->GetUniformLocation("uShadowsEnabled");
    _loc.shadowTexel = _program->GetUniformLocation("uShadowTexel");
    _loc.shadowPass = _program->GetUniformLocation("uShadowPass");
    _loc.skyColour = _program->GetUniformLocation("uSkyColour");
    _loc.ditherMode = _program->GetUniformLocation("uDitherMode");
    _loc.fade = _program->GetUniformLocation("uFade");
}

void Renderer3D::FreeMesh(GpuMesh& mesh)
{
    if (mesh.vbo != 0)
        glCall(glDeleteBuffers, 1, &mesh.vbo);
    if (mesh.vao != 0)
        glCall(glDeleteVertexArrays, 1, &mesh.vao);
    mesh = {};
}

void Renderer3D::FreeChunk(ChunkMesh& chunk)
{
    FreeMesh(chunk.terrain);
    FreeMesh(chunk.water);
    for (int32_t r = 0; r < 4; r++)
    {
        FreeMesh(chunk.sprites[r]);
        chunk.spritesBuilt[r] = false;
    }
    chunk.built = false;
    chunk.dirty = true;
}

void Renderer3D::ResetChunks()
{
    for (auto& chunk : _chunks)
        FreeChunk(chunk);

    _chunkCountX = SceneBuilder::GetChunkCountX();
    _chunkCountY = SceneBuilder::GetChunkCountY();
    _chunks.clear();
    _chunks.resize(static_cast<size_t>(_chunkCountX) * _chunkCountY);
    _hashCursor = 0;
}

void Renderer3D::InvalidateAllChunks()
{
    // Existing meshes refer to texture coordinates that are no longer valid; drawing them would show the wrong
    // pictures, so they are hidden until rebuilt.
    for (auto& chunk : _chunks)
    {
        chunk.dirty = true;
        chunk.built = false;
        for (auto& built : chunk.spritesBuilt)
            built = false;
    }
}

int32_t Renderer3D::PickSpriteRotation(const ChunkMesh& chunk, uint8_t wanted)
{
    // Until the sprites for the current view direction are built, show those of the nearest direction that is.
    static constexpr uint8_t kOrder[] = { 0, 1, 3, 2 };
    for (const auto offset : kOrder)
    {
        const uint8_t rotation = static_cast<uint8_t>((wanted + offset) & 3);
        if (chunk.spritesBuilt[rotation])
            return rotation;
    }
    return -1;
}

void Renderer3D::SetupVertexArray(GLuint vao, GLuint vbo)
{
    glCall(glBindVertexArray, vao);
    glCall(glBindBuffer, GL_ARRAY_BUFFER, vbo);

    const auto stride = glSizeOf<Vertex>();
    auto attrib = [stride](GLint location, GLint size, GLenum type, GLboolean normalised, size_t offset) {
        if (location < 0)
            return;
        glCall(glEnableVertexAttribArray, static_cast<GLuint>(location));
        glCall(
            glVertexAttribPointer, static_cast<GLuint>(location), size, type, normalised, stride,
            reinterpret_cast<void*>(offset));
    };
    attrib(_loc.position, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    attrib(_loc.normal, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    attrib(_loc.uv, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));
    attrib(_loc.layer, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, layer));
    attrib(_loc.colour, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, r));
    attrib(_loc.offset, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, offsetX));
    attrib(_loc.vertexRotation, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, rotation));
    attrib(_loc.flags, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, flags));
}

void Renderer3D::UploadMesh(GpuMesh& mesh, const std::vector<Vertex>& vertices, GLenum usage)
{
    mesh.count = static_cast<GLsizei>(vertices.size());
    if (vertices.empty())
        return;

    if (mesh.vao == 0)
    {
        glCall(glGenVertexArrays, 1, &mesh.vao);
        glCall(glGenBuffers, 1, &mesh.vbo);
        SetupVertexArray(mesh.vao, mesh.vbo);
    }
    glCall(glBindBuffer, GL_ARRAY_BUFFER, mesh.vbo);
    const size_t bytes = vertices.size() * sizeof(Vertex);
    if (usage == GL_STREAM_DRAW && bytes <= mesh.capacity)
    {
        // Reuse the existing allocation for per-frame data.
        glCall(glBufferSubData, GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices.data());
    }
    else
    {
        // Leave headroom for per-frame data so the buffer is not reallocated every time the count changes.
        const size_t capacity = usage == GL_STREAM_DRAW ? bytes + bytes / 2 : bytes;
        glCall(glBufferData, GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, usage);
        glCall(glBufferSubData, GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices.data());
        mesh.capacity = capacity;
    }
}

void Renderer3D::DrawMesh(const GpuMesh& mesh)
{
    if (mesh.count == 0 || mesh.vao == 0)
        return;
    glCall(glBindVertexArray, mesh.vao);
    glCall(glDrawArrays, GL_TRIANGLES, 0, mesh.count);
    _stats.vertices += static_cast<size_t>(mesh.count);
}

void Renderer3D::CheckChunkHashes()
{
    if (_chunks.empty())
        return;

    // Spread change detection over several frames; every chunk is checked roughly every few frames.
    constexpr size_t kChunksPerFrame = 32;
    for (size_t i = 0; i < std::min(kChunksPerFrame, _chunks.size()); i++)
    {
        _hashCursor = (_hashCursor + 1) % _chunks.size();
        auto& chunk = _chunks[_hashCursor];
        if (!chunk.built || chunk.dirty)
            continue;

        const ChunkCoord coord{ static_cast<int32_t>(_hashCursor % _chunkCountX),
                                static_cast<int32_t>(_hashCursor / _chunkCountX) };
        if (SceneBuilder::ComputeChunkHash(coord) != chunk.hash)
            chunk.dirty = true;
    }
}

void Renderer3D::BuildVisibleChunks(
    const Frustum& frustum, const Vec3& cameraPos, uint8_t rotation, float drawDistance, float spriteDistance)
{
    struct Candidate
    {
        size_t index;
        float distance;
    };
    std::vector<Candidate> candidates;

    const float chunkWorldSize = static_cast<float>(kChunkSize * kCoordsXYStep);
    for (int32_t cy = 0; cy < _chunkCountY; cy++)
    {
        for (int32_t cx = 0; cx < _chunkCountX; cx++)
        {
            const size_t index = static_cast<size_t>(cy) * _chunkCountX + cx;
            const auto& chunk = _chunks[index];

            const float centreX = (cx + 0.5f) * chunkWorldSize;
            const float centreY = (cy + 0.5f) * chunkWorldSize;
            const float distance = std::hypot(centreX - cameraPos.x, centreY - cameraPos.y);

            const bool needsTerrain = !chunk.built || chunk.dirty;
            const bool needsSprites = !chunk.spritesBuilt[rotation] && distance <= spriteDistance + chunkWorldSize;
            if (!needsTerrain && !needsSprites)
                continue;
            if (distance > drawDistance + chunkWorldSize)
                continue;

            const AABB area{ { cx * chunkWorldSize, cy * chunkWorldSize, -100.0f },
                             { (cx + 1) * chunkWorldSize, (cy + 1) * chunkWorldSize, GameToWorld(0, 0, 2040).z } };
            // Always build the chunk under the camera, otherwise only what can be seen.
            if (distance > chunkWorldSize && !frustum.Intersects(area))
                continue;

            candidates.push_back({ index, distance });
        }
    }

    if (candidates.empty())
        return;

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });

    SceneBuilder builder(*_atlas);
    const auto start = std::chrono::steady_clock::now();
    // Spend more time when something on screen has nothing to show yet, so holes fill in quickly.
    bool missing = false;
    for (const auto& candidate : candidates)
    {
        const auto& chunk = _chunks[candidate.index];
        if (!chunk.built || PickSpriteRotation(chunk, rotation) < 0)
        {
            missing = true;
            break;
        }
    }
    const auto kBudget = std::chrono::milliseconds(missing ? 30 : 10);
    for (const auto& candidate : candidates)
    {
        const ChunkCoord coord{ static_cast<int32_t>(candidate.index % _chunkCountX),
                                static_cast<int32_t>(candidate.index / _chunkCountX) };
        auto& chunk = _chunks[candidate.index];

        if (!chunk.built || chunk.dirty)
        {
            chunk.hash = SceneBuilder::ComputeChunkHash(coord);
            builder.BuildChunk(coord, _scratch);
            if (_atlas->ConsumeOverflow())
            {
                // The atlas was reset, so every existing mesh refers to stale texture coordinates.
                LOG_VERBOSE("3D texture atlas grew, rebuilding all chunks");
                InvalidateAllChunks();
                builder.BuildChunk(coord, _scratch);
                _atlas->ConsumeOverflow();
            }

            UploadMesh(chunk.terrain, _scratch.solid, GL_STATIC_DRAW);
            UploadMesh(chunk.water, _scratch.transparent, GL_STATIC_DRAW);
            chunk.terrainBounds = _scratch.bounds;
            chunk.built = true;
            chunk.dirty = false;
            // Sprites depend on the same tile elements.
            for (auto& built : chunk.spritesBuilt)
                built = false;
            _stats.chunksBuilt++;
        }

        if (!chunk.spritesBuilt[rotation] && candidate.distance <= spriteDistance + chunkWorldSize)
        {
            builder.BuildChunkSprites(coord, rotation, _scratch);
            if (_atlas->ConsumeOverflow())
            {
                LOG_VERBOSE("3D texture atlas grew, rebuilding all chunks");
                InvalidateAllChunks();
                builder.BuildChunkSprites(coord, rotation, _scratch);
                _atlas->ConsumeOverflow();
                // Terrain of this chunk is stale too; it is rebuilt next frame.
                chunk.dirty = true;
            }
            UploadMesh(chunk.sprites[rotation], _scratch.solid, GL_STATIC_DRAW);
            chunk.spriteBounds[rotation] = _scratch.bounds;
            chunk.spritesBuilt[rotation] = true;
            _stats.spriteChunksBuilt++;
        }

        if (std::chrono::steady_clock::now() - start > kBudget)
            break;
    }
}

void Renderer3D::BuildEntities(const Frustum& frustum, const Vec3& cameraPos, uint8_t rotation, float distance)
{
    const auto& mapSize = getGameState().mapSize;
    const int32_t radius = static_cast<int32_t>(distance / kCoordsXYStep) + 1;
    const int32_t centreX = static_cast<int32_t>(cameraPos.x / kCoordsXYStep);
    const int32_t centreY = static_cast<int32_t>(cameraPos.y / kCoordsXYStep);
    const int32_t x0 = std::max(0, centreX - radius);
    const int32_t y0 = std::max(0, centreY - radius);
    const int32_t x1 = std::min<int32_t>(mapSize.x - 1, centreX + radius);
    const int32_t y1 = std::min<int32_t>(mapSize.y - 1, centreY + radius);

    _entityTiles.clear();
    const float step = static_cast<float>(kCoordsXYStep);
    const float maxZ = GameToWorld(0, 0, 2040).z;
    for (int32_t y = y0; y <= y1; y++)
    {
        for (int32_t x = x0; x <= x1; x++)
        {
            const float dx = (x + 0.5f) * step - cameraPos.x;
            const float dy = (y + 0.5f) * step - cameraPos.y;
            if (dx * dx + dy * dy > distance * distance)
                continue;
            const AABB tileBox{ { x * step, y * step, -100.0f }, { (x + 1) * step, (y + 1) * step, maxZ } };
            if (!frustum.Intersects(tileBox))
                continue;
            _entityTiles.push_back({ x, y });
        }
    }

    SceneBuilder builder(*_atlas);
    builder.BuildEntities(_entityTiles, rotation, _scratch);
    if (_atlas->ConsumeOverflow())
    {
        InvalidateAllChunks();
        builder.BuildEntities(_entityTiles, rotation, _scratch);
        _atlas->ConsumeOverflow();
    }
    UploadMesh(_entities, _scratch.solid, GL_STREAM_DRAW);
    _stats.entityVertices = _scratch.solid.size();
}

std::string Renderer3D::GetStatsText() const
{
    char buffer[384];
    std::snprintf(
        buffer, sizeof(buffer),
        "frame %.1f ms (3D cpu %.1f ms), chunks %zu visible, built %zu terrain + %zu sprite (%.1f ms), %zu vertices drawn, entities %zu "
        "vertices (%.1f ms), atlas %d layers, %u allocations, %u image invalidations",
        _frameMs, _stats.renderMs, _stats.visibleChunks, _stats.chunksBuilt, _stats.spriteChunksBuilt, _stats.buildMs, _stats.vertices,
        _stats.entityVertices, _stats.entityMs, _atlas->GetLayerCount(), _atlas->GetAllocationCount(), _imageInvalidations);
    return buffer;
}

void Renderer3D::Render(int32_t screenWidth, int32_t screenHeight, int32_t vpX, int32_t vpY, int32_t vpWidth, int32_t vpHeight)
{
    _stats = {};
    const auto frameStart = std::chrono::steady_clock::now();
    if (_lastFrame.time_since_epoch().count() != 0)
    {
        const float elapsed = std::chrono::duration<float, std::milli>(frameStart - _lastFrame).count();
        _frameMs = _frameMs == 0.0f ? elapsed : _frameMs * 0.9f + elapsed * 0.1f;
        _lastFrameSeconds = std::min(elapsed / 1000.0f, 0.1f);
    }
    _lastFrame = frameStart;

    if (_framebuffer == nullptr || static_cast<int32_t>(_framebuffer->GetWidth()) != screenWidth
        || static_cast<int32_t>(_framebuffer->GetHeight()) != screenHeight)
    {
        _framebuffer = std::make_unique<OpenGLFramebuffer>(screenWidth, screenHeight, true, false);
    }

    if (SceneBuilder::GetChunkCountX() != _chunkCountX || SceneBuilder::GetChunkCountY() != _chunkCountY)
        ResetChunks();

    if (_imagesChanged)
    {
        // Image indices may now refer to different pictures.
        _imagesChanged = false;
        _atlas->Clear();
        InvalidateAllChunks();
    }

    if (_atlas->SetSmooth(Render3D::GetCamera().GetSettings().smoothSprites))
        InvalidateAllChunks();

    // Follow the ridden train with the vehicle positions interpolated for this frame.
    Render3D::UpdateRideCamera();

    auto& camera = Render3D::GetCamera();
    const Vec3 cameraPos = camera.GetPosition();
    const float drawDistance = std::max(camera.GetSettings().drawDistance, 800.0f);
    // Scenery and people are small; drawing them as far as the terrain costs a lot for little visible detail.
    const float spriteDistance = drawDistance * 0.75f;
    const float entityDistance = std::min(drawDistance * 0.4f, 1600.0f);

    // Lighting follows the game's day/night cycle and weather gloom.
    const float night = std::clamp(gDayNightCycle, 0.0f, 1.0f);
    const float gloom = std::clamp(getGameState().weatherCurrent.weatherGloom / 3.0f, 0.0f, 1.0f);
    const Vec3 dayHorizon{ 0.74f, 0.84f, 0.95f };
    const Vec3 dayZenith{ 0.30f, 0.52f, 0.86f };
    const Vec3 gloomSky{ 0.45f, 0.48f, 0.52f };
    const Vec3 nightHorizon{ 0.08f, 0.10f, 0.19f };
    const Vec3 nightZenith{ 0.02f, 0.03f, 0.08f };
    const Vec3 sky = Mix(Mix(dayHorizon, gloomSky, gloom), nightHorizon, night);
    const Vec3 zenith = Mix(Mix(dayZenith, gloomSky * 0.9f, gloom), nightZenith, night);
    const Vec3 ambient = Mix(Vec3{ 0.62f, 0.62f, 0.64f }, Vec3{ 0.16f, 0.18f, 0.28f }, night);
    const Vec3 sun = Mix(Vec3{ 0.55f, 0.53f, 0.48f } * (1.0f - 0.5f * gloom), Vec3{ 0.08f, 0.08f, 0.12f }, night);
    // Afternoon sun from the side of the default isometric view, so shadows fall where they can be seen.
    const Vec3 sunDirection = Normalize(Vec3{ 0.55f, -0.45f, 0.7f });

    _framebuffer->Bind();
    glCall(glClearColor, sky.x, sky.y, sky.z, 1.0f);
    glCall(glClearDepth, 1.0);
    glCall(glClear, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (vpWidth <= 0 || vpHeight <= 0)
        return;

    // GL viewport origin is bottom-left.
    glCall(glViewport, vpX, screenHeight - (vpY + vpHeight), vpWidth, vpHeight);

    const float aspect = static_cast<float>(vpWidth) / static_cast<float>(vpHeight);
    const Mat4 view = camera.GetViewMatrix();
    const Mat4 projection = camera.GetProjectionMatrix(aspect, 2.0f, drawDistance + 512.0f);
    const Mat4 viewProj = projection * view;
    const Frustum frustum(viewProj);

    // Pick the pre-rendered sprite rotation whose isometric view direction is closest to the camera's.
    const Vec3 forward = camera.GetForward();
    static constexpr float kIsoDirections[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    uint8_t closest = 0;
    float best = -1e9f;
    for (uint8_t r = 0; r < 4; r++)
    {
        const float d = forward.x * kIsoDirections[r][0] + forward.y * kIsoDirections[r][1];
        if (d > best)
        {
            best = d;
            closest = r;
        }
    }

    // Looking straight along a map axis is exactly between two rotations, which is also how buildings are usually
    // looked at. Only switch once well past the boundary, so small camera movements there do not flip every sprite,
    // and cross-fade when switching instead of popping.
    const float horizontal = std::hypot(forward.x, forward.y);
    if (!_hasIsoRotation)
    {
        _isoRotation = closest;
        _hasIsoRotation = true;
    }
    else if (closest != _isoRotation && horizontal > 0.2f)
    {
        const float yaw = std::atan2(forward.y, forward.x);
        const float current = std::atan2(kIsoDirections[_isoRotation][1], kIsoDirections[_isoRotation][0]);
        const float difference = std::abs(std::remainder(yaw - current, 2.0f * kPi));
        constexpr float kSwitchAngle = (45.0f + 10.0f) * kPi / 180.0f;
        if (difference > kSwitchAngle)
        {
            _fadeFrom = _isoRotation;
            _fade = 0.0f;
            _isoRotation = closest;
        }
    }
    if (_fadeFrom >= 0)
    {
        constexpr float kFadeSeconds = 0.3f;
        _fade += _lastFrameSeconds / kFadeSeconds;
        if (_fade >= 1.0f)
        {
            _fade = 1.0f;
            _fadeFrom = -1;
        }
    }
    const uint8_t isoRotation = _isoRotation;

    const auto buildStart = std::chrono::steady_clock::now();
    CheckChunkHashes();
    BuildVisibleChunks(frustum, cameraPos, isoRotation, drawDistance, spriteDistance);
    const auto entityStart = std::chrono::steady_clock::now();
    BuildEntities(frustum, cameraPos, isoRotation, entityDistance);
    const auto buildEnd = std::chrono::steady_clock::now();
    _stats.buildMs = std::chrono::duration<float, std::milli>(entityStart - buildStart).count();
    _stats.entityMs = std::chrono::duration<float, std::milli>(buildEnd - entityStart).count();
    _atlas->FinishUploads();

    _time += 1.0f / 60.0f;

    _program->Use();
    glCall(glUniform1i, _loc.texture, 0);
    glCall(glUniform1i, _loc.shadowMap, 1);
    glCall(glUniform1i, _loc.ditherMode, 0);
    OpenGLAPI::SetTexture(0, GL_TEXTURE_2D_ARRAY, _atlas->GetTexture());
    SetUniform(_loc.cameraRight, camera.GetRight());
    glCall(glUniform1f, _loc.rotation, static_cast<float>(isoRotation));

    // The sun is only bright enough to cast shadows by day and in clear weather.
    const float sunStrength = (1.0f - night) * (1.0f - 0.6f * gloom);
    const bool shadows = camera.GetSettings().shadows && sunStrength > 0.2f && EnsureShadowMap();
    Mat4 lightViewProj = Mat4::Identity();
    if (shadows)
    {
        const float shadowRadius = std::clamp(drawDistance * 0.35f, 400.0f, 1400.0f);
        lightViewProj = RenderShadowMap(cameraPos, forward, sunDirection, isoRotation, shadowRadius);
        glCall(glUniform1f, _loc.shadowTexel, 1.0f / kShadowMapSize);
    }
    // The sampler must always see a depth texture, even when shadows are off.
    if (_shadowTexture != 0)
    {
        glCall(glActiveTexture, GL_TEXTURE1);
        glCall(glBindTexture, GL_TEXTURE_2D, _shadowTexture);
        glCall(glActiveTexture, GL_TEXTURE0);
    }
    glCall(glUniform1i, _loc.shadowsEnabled, shadows ? 1 : 0);
    glCall(glUniformMatrix4fv, _loc.lightViewProj, 1, GL_FALSE, lightViewProj.m.data());

    // Back to the screen: sky first, then the scene on top.
    _framebuffer->Bind();
    glCall(glViewport, vpX, screenHeight - (vpY + vpHeight), vpWidth, vpHeight);
    DrawSky(viewProj, zenith, sky, sunDirection, Vec3{ 1.0f, 0.95f, 0.8f }, sunStrength);

    _program->Use();
    SetUniform(_loc.skyColour, Mix(sky, zenith, 0.5f));
    glCall(glUniformMatrix4fv, _loc.viewProj, 1, GL_FALSE, viewProj.m.data());
    SetUniform(_loc.sunDirection, sunDirection);
    SetUniform(_loc.sunColour, sun);
    SetUniform(_loc.ambientColour, ambient);
    SetUniform(_loc.fogColour, sky);
    glCall(glUniform1f, _loc.fogStart, drawDistance * 0.35f);
    glCall(glUniform1f, _loc.fogEnd, drawDistance);
    SetUniform(_loc.cameraPos, cameraPos);
    glCall(glUniform1f, _loc.time, _time);

    const float chunkRadius = static_cast<float>(kChunkSize * kCoordsXYStep);
    std::vector<size_t> visible;
    for (size_t i = 0; i < _chunks.size(); i++)
    {
        auto& chunk = _chunks[i];
        if (!chunk.built)
            continue;

        chunk.bounds = chunk.terrainBounds;
        chunk.drawRotation = PickSpriteRotation(chunk, isoRotation);
        if (chunk.drawRotation >= 0 && chunk.sprites[chunk.drawRotation].count > 0)
        {
            const auto& sb = chunk.spriteBounds[chunk.drawRotation];
            chunk.bounds.min = { std::min(chunk.bounds.min.x, sb.min.x), std::min(chunk.bounds.min.y, sb.min.y),
                                 std::min(chunk.bounds.min.z, sb.min.z) };
            chunk.bounds.max = { std::max(chunk.bounds.max.x, sb.max.x), std::max(chunk.bounds.max.y, sb.max.y),
                                 std::max(chunk.bounds.max.z, sb.max.z) };
        }
        if (chunk.terrain.count == 0 && chunk.water.count == 0 && (chunk.drawRotation < 0 || chunk.sprites[chunk.drawRotation].count == 0))
            continue;

        const float cx = (chunk.bounds.min.x + chunk.bounds.max.x) * 0.5f;
        const float cy = (chunk.bounds.min.y + chunk.bounds.max.y) * 0.5f;
        if (std::hypot(cx - cameraPos.x, cy - cameraPos.y) > drawDistance + chunkRadius)
            continue;
        if (!frustum.Intersects(chunk.bounds))
            continue;
        visible.push_back(i);
    }
    _stats.visibleChunks = visible.size();

    // Solid pass: terrain, then alpha-tested sprites, then entities.
    glCall(glEnable, GL_DEPTH_TEST);
    glCall(glDepthFunc, GL_LESS);
    glCall(glDepthMask, GL_TRUE);
    glCall(glDisable, GL_BLEND);
    glCall(glEnable, GL_CULL_FACE);
    glCall(glCullFace, GL_BACK);
    glCall(glUniform1i, _loc.transparentPass, 0);
    for (auto i : visible)
        DrawMesh(_chunks[i].terrain);
    for (auto i : visible)
    {
        const auto& chunk = _chunks[i];
        if (chunk.drawRotation < 0)
            continue;
        const bool crossFade = _fadeFrom >= 0 && chunk.drawRotation == isoRotation && chunk.spritesBuilt[_fadeFrom];
        if (!crossFade)
        {
            DrawMesh(chunk.sprites[chunk.drawRotation]);
            continue;
        }
        glCall(glUniform1f, _loc.fade, _fade);
        glCall(glUniform1i, _loc.ditherMode, 1);
        DrawMesh(chunk.sprites[isoRotation]);
        glCall(glUniform1i, _loc.ditherMode, 2);
        DrawMesh(chunk.sprites[_fadeFrom]);
        glCall(glUniform1i, _loc.ditherMode, 0);
    }
    DrawMesh(_entities);

    // Transparent pass, back to front.
    std::sort(visible.begin(), visible.end(), [this, &cameraPos](size_t a, size_t b) {
        auto dist = [this, &cameraPos](size_t i) {
            const auto& bb = _chunks[i].bounds;
            return std::hypot((bb.min.x + bb.max.x) * 0.5f - cameraPos.x, (bb.min.y + bb.max.y) * 0.5f - cameraPos.y);
        };
        return dist(a) > dist(b);
    });
    glCall(glEnable, GL_BLEND);
    glCall(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glCall(glDepthMask, GL_FALSE);
    glCall(glDisable, GL_CULL_FACE);
    glCall(glUniform1i, _loc.transparentPass, 1);
    for (auto i : visible)
        DrawMesh(_chunks[i].water);

    // Restore the state the 2D renderer expects.
    glCall(glDepthMask, GL_TRUE);
    glCall(glDisable, GL_BLEND);
    glCall(glDisable, GL_DEPTH_TEST);
    glCall(glBindVertexArray, 0);
    _stats.renderMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
}

    #pragma endregion

#endif /* DISABLE_OPENGL */
