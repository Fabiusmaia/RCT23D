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
#include <functional>

namespace OpenRCT2::Render3D
{
    enum class CameraMode : uint8_t
    {
        // Free flying camera, only kept above the ground.
        freeFly,
        // Walking at eye height over terrain, paths and track.
        firstPerson,
    };

    struct CameraSettings
    {
        float fieldOfViewDegrees = 70.0f;
        // World units (1/32 of a tile) per second.
        float moveSpeed = 256.0f;
        float sprintMultiplier = 3.0f;
        // Radians per pixel of mouse movement.
        float mouseSensitivity = 0.0035f;
        bool invertY = false;
        // Height of the eye above the ground in first person mode (world units).
        float eyeHeight = 22.0f;
        // How far terrain is drawn (world units); scenery and people are drawn over shorter distances.
        float drawDistance = 3200.0f;
        // Doubles sprites with a pixel art filter and smooths them when magnified.
        bool smoothSprites = true;
        // Sun shadows.
        bool shadows = true;
    };

    struct CameraInput
    {
        bool forward{};
        bool backward{};
        bool left{};
        bool right{};
        bool up{};
        bool down{};
        bool sprint{};
    };

    struct CameraBounds
    {
        float minX{};
        float minY{};
        float maxX{};
        float maxY{};
        float maxZ{};
    };

    class Camera3D
    {
    public:
        // Returns the walkable/solid height (world units) at the given world x/y.
        using GroundHeightFunc = std::function<float(float x, float y)>;

        // Walking collision: given where the feet are and where they want to go (world units), returns whether the step
        // is allowed and, if so, the height of the ground at the destination in `outGroundZ`.
        using WalkFunc = std::function<bool(const Vec3& feet, float toX, float toY, float& outGroundZ)>;

    private:
        Vec3 _position{ 0, 0, 100 };
        // Yaw around the world Z axis, 0 looks along +X.
        float _yaw = 0;
        float _pitch = -0.4f;
        CameraMode _mode = CameraMode::freeFly;
        CameraSettings _settings{};
        CameraBounds _bounds{ -1e9f, -1e9f, 1e9f, 1e9f, 1e9f };

        // Riding a vehicle: the camera follows a moving frame, and mouse look turns the head relative to it.
        bool _attached{};
        Vec3 _attachForward{ 1, 0, 0 };
        Vec3 _attachUp{ 0, 0, 1 };
        float _lookYaw{};
        float _lookPitch{};

        WalkFunc _walk;

    public:
        static constexpr float kMinPitch = -1.55f;
        static constexpr float kMaxPitch = 1.55f;
        static constexpr float kGroundClearance = 6.0f;

        const Vec3& GetPosition() const
        {
            return _position;
        }
        void SetPosition(const Vec3& pos)
        {
            _position = pos;
        }
        float GetYaw() const
        {
            return _yaw;
        }
        float GetPitch() const
        {
            return _pitch;
        }
        void SetOrientation(float yaw, float pitch);

        CameraMode GetMode() const
        {
            return _mode;
        }
        void SetMode(CameraMode mode)
        {
            _mode = mode;
        }

        CameraSettings& GetSettings()
        {
            return _settings;
        }
        const CameraSettings& GetSettings() const
        {
            return _settings;
        }

        void SetBounds(const CameraBounds& bounds)
        {
            _bounds = bounds;
        }

        // Collision used in first person mode. Without one, walking follows `groundHeight` with no obstacles.
        void SetWalkCollision(WalkFunc walk)
        {
            _walk = std::move(walk);
        }

        Vec3 GetForward() const;
        Vec3 GetRight() const;
        Vec3 GetUp() const;

        // Puts the camera at a moving frame (e.g. a rider's eyes). Called every frame while riding; movement input is
        // ignored and mouse look is relative to the frame, so the view turns over through loops.
        void Attach(const Vec3& position, const Vec3& forward, const Vec3& up);
        // Returns to free movement where the camera currently is, looking the same way (but level).
        void Detach();
        bool IsAttached() const
        {
            return _attached;
        }

        // Applies a mouse movement in screen pixels.
        void Rotate(float deltaX, float deltaY);

        // Moves the camera by the given input over deltaTime seconds and applies the constraints.
        void Update(const CameraInput& input, float deltaTime, const GroundHeightFunc& groundHeight);

        // Keeps the camera inside the bounds and above the ground.
        void ApplyConstraints(const GroundHeightFunc& groundHeight);

        // Places the camera so that it looks at the target from the given distance.
        void LookAtTarget(const Vec3& target, float distance);

        Mat4 GetViewMatrix() const;
        Mat4 GetProjectionMatrix(float aspect, float zNear, float zFar) const;
    };
} // namespace OpenRCT2::Render3D
