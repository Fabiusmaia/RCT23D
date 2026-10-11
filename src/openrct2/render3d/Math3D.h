/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <array>
#include <cmath>

namespace OpenRCT2::Render3D
{
    constexpr float kPi = 3.14159265358979323846f;

    // The game's z unit is not the same length as its x/y unit. In the 2:1 dimetric projection used by RCT2 one z unit
    // is drawn as one pixel, while a vertical edge of length L (in x/y units) is drawn as L * cos(30deg) * sqrt(2) pixels.
    // Converting back gives sqrt(2/3) x/y units per z unit.
    constexpr float kZScale = 0.81649658f;

    // Size of one sprite pixel in world units, horizontally (along the screen x axis) and vertically.
    constexpr float kSpritePixelWidth = 0.70710678f;
    constexpr float kSpritePixelHeight = kZScale;

    struct Vec3
    {
        float x{};
        float y{};
        float z{};

        constexpr Vec3() = default;
        constexpr Vec3(float x_, float y_, float z_)
            : x(x_)
            , y(y_)
            , z(z_)
        {
        }

        constexpr Vec3 operator+(const Vec3& o) const
        {
            return { x + o.x, y + o.y, z + o.z };
        }
        constexpr Vec3 operator-(const Vec3& o) const
        {
            return { x - o.x, y - o.y, z - o.z };
        }
        constexpr Vec3 operator*(float s) const
        {
            return { x * s, y * s, z * s };
        }
        constexpr Vec3 operator-() const
        {
            return { -x, -y, -z };
        }
        Vec3& operator+=(const Vec3& o)
        {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }
    };

    constexpr float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    inline float Length(const Vec3& v)
    {
        return std::sqrt(Dot(v, v));
    }

    inline Vec3 Normalize(const Vec3& v)
    {
        const float len = Length(v);
        if (len <= 1e-12f)
            return { 0, 0, 0 };
        return v * (1.0f / len);
    }

    // Column-major 4x4 matrix, matching OpenGL conventions (m[column * 4 + row]).
    struct Mat4
    {
        std::array<float, 16> m{};

        static constexpr Mat4 Identity()
        {
            Mat4 r;
            r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
            return r;
        }

        constexpr float& At(int row, int col)
        {
            return m[col * 4 + row];
        }
        constexpr float At(int row, int col) const
        {
            return m[col * 4 + row];
        }

        Mat4 operator*(const Mat4& o) const
        {
            Mat4 r;
            for (int col = 0; col < 4; col++)
            {
                for (int row = 0; row < 4; row++)
                {
                    float sum = 0;
                    for (int k = 0; k < 4; k++)
                        sum += At(row, k) * o.At(k, col);
                    r.At(row, col) = sum;
                }
            }
            return r;
        }

        // Transforms a point (w = 1) and returns clip space xyzw.
        std::array<float, 4> Transform(const Vec3& p) const
        {
            std::array<float, 4> r{};
            for (int row = 0; row < 4; row++)
                r[row] = At(row, 0) * p.x + At(row, 1) * p.y + At(row, 2) * p.z + At(row, 3);
            return r;
        }
    };

    // General 4x4 inverse (cofactor expansion). Returns identity for a singular matrix.
    inline Mat4 Inverse(const Mat4& mat)
    {
        const auto& m = mat.m;
        std::array<float, 16> inv{};
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11]
            - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11]
            + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11]
            - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13]
            - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11]
            + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11]
            - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11]
            + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10]
            - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]
            - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]
            + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]
            - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]
            + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7]
            + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7]
            - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7]
            + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6]
            - m[8] * m[2] * m[5];

        const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(det) < 1e-20f)
            return Mat4::Identity();
        Mat4 result;
        for (size_t i = 0; i < 16; i++)
            result.m[i] = inv[i] / det;
        return result;
    }

    inline Mat4 Perspective(float fovYRadians, float aspect, float zNear, float zFar)
    {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r;
        r.At(0, 0) = f / aspect;
        r.At(1, 1) = f;
        r.At(2, 2) = (zFar + zNear) / (zNear - zFar);
        r.At(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
        r.At(3, 2) = -1.0f;
        return r;
    }

    inline Mat4 Orthographic(float left, float right, float bottom, float top, float zNear, float zFar)
    {
        Mat4 r = Mat4::Identity();
        r.At(0, 0) = 2.0f / (right - left);
        r.At(1, 1) = 2.0f / (top - bottom);
        r.At(2, 2) = -2.0f / (zFar - zNear);
        r.At(0, 3) = -(right + left) / (right - left);
        r.At(1, 3) = -(top + bottom) / (top - bottom);
        r.At(2, 3) = -(zFar + zNear) / (zFar - zNear);
        return r;
    }

    inline Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 f = Normalize(target - eye);
        const Vec3 s = Normalize(Cross(f, up));
        const Vec3 u = Cross(s, f);

        Mat4 r = Mat4::Identity();
        r.At(0, 0) = s.x;
        r.At(0, 1) = s.y;
        r.At(0, 2) = s.z;
        r.At(1, 0) = u.x;
        r.At(1, 1) = u.y;
        r.At(1, 2) = u.z;
        r.At(2, 0) = -f.x;
        r.At(2, 1) = -f.y;
        r.At(2, 2) = -f.z;
        r.At(0, 3) = -Dot(s, eye);
        r.At(1, 3) = -Dot(u, eye);
        r.At(2, 3) = Dot(f, eye);
        return r;
    }

    struct AABB
    {
        Vec3 min{};
        Vec3 max{};
    };

    // View frustum extracted from a view-projection matrix (Gribb/Hartmann method).
    class Frustum
    {
    private:
        struct Plane
        {
            Vec3 n;
            float d;
        };
        std::array<Plane, 6> _planes{};

    public:
        Frustum() = default;

        explicit Frustum(const Mat4& vp)
        {
            auto row = [&vp](int r) { return std::array<float, 4>{ vp.At(r, 0), vp.At(r, 1), vp.At(r, 2), vp.At(r, 3) }; };
            const auto r0 = row(0);
            const auto r1 = row(1);
            const auto r2 = row(2);
            const auto r3 = row(3);
            const std::array<std::array<float, 4>, 6> raw = { {
                { r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3] },
                { r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3] },
                { r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3] },
                { r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3] },
                { r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3] },
                { r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3] },
            } };
            for (size_t i = 0; i < raw.size(); i++)
            {
                Vec3 n{ raw[i][0], raw[i][1], raw[i][2] };
                const float len = Length(n);
                const float inv = len > 0 ? 1.0f / len : 0.0f;
                _planes[i] = { n * inv, raw[i][3] * inv };
            }
        }

        bool Intersects(const AABB& box) const
        {
            for (const auto& p : _planes)
            {
                // Pick the box corner furthest along the plane normal.
                const Vec3 v{ p.n.x >= 0 ? box.max.x : box.min.x, p.n.y >= 0 ? box.max.y : box.min.y,
                              p.n.z >= 0 ? box.max.z : box.min.z };
                if (Dot(p.n, v) + p.d < 0)
                    return false;
            }
            return true;
        }

        bool Contains(const Vec3& point, float radius = 0.0f) const
        {
            for (const auto& p : _planes)
            {
                if (Dot(p.n, point) + p.d < -radius)
                    return false;
            }
            return true;
        }
    };

    // Converts game coordinates (x/y in 1/32 tile units, z in game z units) to 3D world space.
    constexpr Vec3 GameToWorld(float x, float y, float z)
    {
        return { x, y, z * kZScale };
    }

    constexpr float WorldZToGame(float worldZ)
    {
        return worldZ / kZScale;
    }
} // namespace OpenRCT2::Render3D
