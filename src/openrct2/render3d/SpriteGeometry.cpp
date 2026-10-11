/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SpriteGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace OpenRCT2::Render3D
{
    namespace
    {
        struct GamePoint
        {
            float x;
            float y;
            float z;
        };

        // Emits a textured quad whose texture coordinates come from projecting each corner into the sprite.
        void EmitProjectedQuad(
            std::vector<Vertex>& out, const SpritePlacement& placement, const TextureRegion& texture,
            const std::array<GamePoint, 4>& corners, const Vec3& outward)
        {
            std::array<Vertex, 4> v;
            for (size_t i = 0; i < corners.size(); i++)
            {
                const auto& c = corners[i];
                const auto screen = ProjectIsometric(placement.rotation, c.x, c.y, c.z);
                const float u = (screen.x - placement.left) / placement.width;
                const float t = (screen.y - placement.top) / placement.height;

                v[i].position = GameToWorld(c.x, c.y, c.z);
                v[i].u = texture.u0 + (texture.u1 - texture.u0) * u;
                v[i].v = texture.v0 + (texture.v1 - texture.v0) * t;
                v[i].layer = texture.layer;
                v[i].flags = kVertexFlagSprite;
                // Faces usually reach beyond the sprite's pixels. Sprite geometry is not lit directionally and not a
                // billboard, so the normal and offset carry the sprite's atlas rectangle for the shader to clip to.
                v[i].offsetX = texture.u0;
                v[i].offsetY = texture.v0;
                v[i].normal = { texture.u1, texture.v1, 0.0f };
            }

            // Counter-clockwise when seen from the outward side.
            const Vec3 n = Cross(v[1].position - v[0].position, v[2].position - v[0].position);
            if (Dot(n, outward) < 0)
            {
                std::swap(v[1], v[3]);
            }
            out.push_back(v[0]);
            out.push_back(v[1]);
            out.push_back(v[2]);
            out.push_back(v[0]);
            out.push_back(v[2]);
            out.push_back(v[3]);
        }

        GamePoint FromRotated(uint8_t rotation, float a, float b, float z)
        {
            GamePoint p;
            RotateXY(static_cast<uint8_t>((4 - rotation) & 3), a, b, p.x, p.y);
            p.z = z;
            return p;
        }

        Vec3 OutwardFromRotated(uint8_t rotation, float a, float b)
        {
            float x, y;
            RotateXY(static_cast<uint8_t>((4 - rotation) & 3), a, b, x, y);
            return { x, y, 0.0f };
        }
    } // namespace

    void RotateXY(uint8_t rotation, float x, float y, float& outX, float& outY)
    {
        switch (rotation & 3)
        {
            default:
            case 0:
                outX = x;
                outY = y;
                break;
            case 1:
                outX = y;
                outY = -x;
                break;
            case 2:
                outX = -x;
                outY = -y;
                break;
            case 3:
                outX = -y;
                outY = x;
                break;
        }
    }

    ScreenPointF ProjectIsometric(uint8_t rotation, float x, float y, float z)
    {
        float rx, ry;
        RotateXY(rotation, x, y, rx, ry);
        return { ry - rx, (rx + ry) * 0.5f - z };
    }

    void AddSpriteBox(std::vector<Vertex>& out, const SpritePlacement& placement, const TextureRegion& texture)
    {
        if (!texture.IsValid() || placement.width <= 0 || placement.height <= 0)
            return;

        const uint8_t r = placement.rotation;

        // Work in the rotated frame (a, b), where the isometric camera looks from +a, +b, +z.
        float a0, b0, a1, b1;
        RotateXY(r, placement.boundsMin.x, placement.boundsMin.y, a0, b0);
        RotateXY(r, placement.boundsMax.x, placement.boundsMax.y, a1, b1);
        if (a0 > a1)
            std::swap(a0, a1);
        if (b0 > b1)
            std::swap(b0, b1);
        a1 = std::max(a1, a0 + 1.0f);
        b1 = std::max(b1, b0 + 1.0f);
        const float z0 = placement.boundsMin.z;
        float z1 = std::max(placement.boundsMax.z, z0 + 1.0f);

        const float right = placement.left + placement.width;

        // Grow backwards (away from the camera) so the front faces stay where the object really is.
        const float boxLeft = b0 - a1;
        if (boxLeft > placement.left)
            b0 -= boxLeft - placement.left;
        const float boxRight = b1 - a0;
        if (boxRight < right)
            a0 -= right - boxRight;

        // Grow upwards until the top outline of the box is above the sprite at its left and right corners.
        const float cornerZ = std::max((a1 + b0) * 0.5f, (a0 + b1) * 0.5f) - placement.top;
        if (cornerZ - z1 <= kMaxTopFaceRaise)
        {
            // The box fits the sprite (buildings, track, stalls): keep a roof.
            z1 = std::max(z1, cornerZ);
            EmitProjectedQuad(
                out, placement, texture,
                { FromRotated(r, a0, b0, z1), FromRotated(r, a1, b0, z1), FromRotated(r, a1, b1, z1),
                  FromRotated(r, a0, b1, z1) },
                { 0, 0, 1 });
        }
        else
        {
            // The sprite is much taller than its box (trees, roofs, flat rides, people). A roof would catch the upper
            // pixels far behind the object. Extending the front faces upwards would put the top of the sprite (the
            // peak of a roof, the crown of a tree) on the front corner, which seen from a map axis is at one side of
            // the object, a different side for each isometric view. Instead the part above the box goes on a vertical
            // card along the diagonal between the side corners, which is in the middle of the object from any view.
            // The card reaches down to the top edges of the front faces, so everything above them stays upright.
            const float wa = a1 - a0;
            const float wb = b1 - b0;
            // Point of the vertical plane through (a1, b0) and (a0, b1) that projects to screen (sx, sy), no lower than
            // minZ.
            auto onCard = [&](float sx, float sy, float minZ) {
                const float t = (sx - (b0 - a1)) / (wa + wb);
                const float a = a1 - t * wa;
                const float b = b0 + t * wb;
                return FromRotated(r, a, b, std::max(minZ, (a + b) * 0.5f - sy));
            };
            const float leftX = b0 - a1;
            const float frontX = b1 - a1;
            const float rightX = b1 - a0;
            const float frontY = (a1 + b1) * 0.5f - z1;
            const GamePoint front = onCard(frontX, frontY, z0);
            EmitProjectedQuad(
                out, placement, texture,
                { onCard(leftX, placement.top, z1), onCard(frontX, placement.top, front.z), front,
                  FromRotated(r, a1, b0, z1) },
                OutwardFromRotated(r, wb, wa));
            EmitProjectedQuad(
                out, placement, texture,
                { onCard(frontX, placement.top, front.z), onCard(rightX, placement.top, z1), FromRotated(r, a0, b1, z1),
                  front },
                OutwardFromRotated(r, wb, wa));
        }
        // Face towards +a.
        EmitProjectedQuad(
            out, placement, texture,
            { FromRotated(r, a1, b0, z0), FromRotated(r, a1, b1, z0), FromRotated(r, a1, b1, z1), FromRotated(r, a1, b0, z1) },
            OutwardFromRotated(r, 1, 0));
        // Face towards +b.
        EmitProjectedQuad(
            out, placement, texture,
            { FromRotated(r, a0, b1, z0), FromRotated(r, a1, b1, z0), FromRotated(r, a1, b1, z1), FromRotated(r, a0, b1, z1) },
            OutwardFromRotated(r, 0, 1));
    }

    SpriteBoxGrowth MeasureSpriteBoxGrowth(const SpritePlacement& placement)
    {
        float a0, b0, a1, b1;
        RotateXY(placement.rotation, placement.boundsMin.x, placement.boundsMin.y, a0, b0);
        RotateXY(placement.rotation, placement.boundsMax.x, placement.boundsMax.y, a1, b1);
        if (a0 > a1)
            std::swap(a0, a1);
        if (b0 > b1)
            std::swap(b0, b1);

        SpriteBoxGrowth growth;
        const float right = placement.left + placement.width;
        growth.horizontal = std::max(0.0f, (b0 - a1) - placement.left) + std::max(0.0f, right - (b1 - a0));
        // The sprite's bottom edge should be near the box; a sprite drawn far above or below it does not belong to it.
        const float boxBottomY = (a1 + b1) * 0.5f - placement.boundsMin.z;
        const float bottom = placement.top + placement.height;
        growth.vertical = std::abs(bottom - boxBottomY);
        return growth;
    }

    bool IsSpriteBoxMismatched(const SpritePlacement& placement)
    {
        const auto growth = MeasureSpriteBoxGrowth(placement);
        return growth.horizontal > placement.width + 32.0f || growth.vertical > placement.height + 64.0f;
    }

    void AddSpriteCard(std::vector<Vertex>& out, const SpritePlacement& placement, const TextureRegion& texture)
    {
        if (!texture.IsValid() || placement.width <= 0 || placement.height <= 0)
            return;

        const uint8_t r = placement.rotation;
        float ca, cb;
        RotateXY(
            r, (placement.boundsMin.x + placement.boundsMax.x) * 0.5f, (placement.boundsMin.y + placement.boundsMax.y) * 0.5f,
            ca, cb);
        // Points on the plane a + b = k project to screen x = b - a and screen y = k / 2 - z.
        const float k = ca + cb;
        auto onCard = [&](float sx, float sy) { return FromRotated(r, (k - sx) * 0.5f, (k + sx) * 0.5f, k * 0.5f - sy); };

        const float right = placement.left + placement.width;
        const float bottom = placement.top + placement.height;
        EmitProjectedQuad(
            out, placement, texture,
            { onCard(placement.left, placement.top), onCard(right, placement.top), onCard(right, bottom),
              onCard(placement.left, bottom) },
            OutwardFromRotated(r, 1, 1));
    }

    void AddSpriteOnPlane(
        std::vector<Vertex>& out, const SpritePlacement& placement, const GroundPlane& plane, const TextureRegion& texture)
    {
        if (!texture.IsValid() || placement.width <= 0 || placement.height <= 0)
            return;

        const uint8_t r = placement.rotation;

        // In the rotated frame the plane is z = baseZ + h1 * a + h2 * b.
        float h1, h2;
        RotateXY(r, plane.gradientX, plane.gradientY, h1, h2);
        const float denominator = 2.0f - 2.0f * h1 - 2.0f * h2;
        if (std::abs(denominator) < 1e-3f)
            return;

        // Solve b - a = sx and (a + b) / 2 - z(a, b) = sy for a point on the plane.
        auto unproject = [&](float sx, float sy) {
            const float a = (2.0f * sy + 2.0f * plane.baseZ + 2.0f * h2 * sx - sx) / denominator;
            const float b = a + sx;
            return FromRotated(r, a, b, plane.baseZ + h1 * a + h2 * b);
        };

        const float right = placement.left + placement.width;
        const float bottom = placement.top + placement.height;
        EmitProjectedQuad(
            out, placement, texture,
            { unproject(placement.left, placement.top), unproject(right, placement.top), unproject(right, bottom),
              unproject(placement.left, bottom) },
            { 0, 0, 1 });
    }
} // namespace OpenRCT2::Render3D
