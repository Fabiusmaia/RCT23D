/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Camera3D.h"

#include <algorithm>

namespace OpenRCT2::Render3D
{
    void Camera3D::SetOrientation(float yaw, float pitch)
    {
        _yaw = std::fmod(yaw, 2.0f * kPi);
        _pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
    }

    Vec3 Camera3D::GetForward() const
    {
        if (_attached)
        {
            // Turn the head left/right around the frame's up axis, then up/down.
            const Vec3 left = Normalize(Cross(_attachUp, _attachForward));
            const Vec3 turned = _attachForward * std::cos(_lookYaw) + left * std::sin(_lookYaw);
            return Normalize(turned * std::cos(_lookPitch) + _attachUp * std::sin(_lookPitch));
        }
        const float cp = std::cos(_pitch);
        return { cp * std::cos(_yaw), cp * std::sin(_yaw), std::sin(_pitch) };
    }

    Vec3 Camera3D::GetUp() const
    {
        if (_attached)
        {
            const Vec3 forward = GetForward();
            const Vec3 up = _attachUp - forward * Dot(forward, _attachUp);
            return Length(up) > 1e-3f ? Normalize(up) : _attachUp;
        }
        return { 0, 0, 1 };
    }

    Vec3 Camera3D::GetRight() const
    {
        return Normalize(Cross(GetForward(), GetUp()));
    }

    void Camera3D::Rotate(float deltaX, float deltaY)
    {
        const float sens = _settings.mouseSensitivity;
        const float dy = _settings.invertY ? deltaY : -deltaY;
        if (_attached)
        {
            _lookYaw = std::clamp(_lookYaw - deltaX * sens, -kPi * 0.9f, kPi * 0.9f);
            _lookPitch = std::clamp(_lookPitch + dy * sens, kMinPitch, kMaxPitch);
            return;
        }
        SetOrientation(_yaw - deltaX * sens, _pitch + dy * sens);
    }

    void Camera3D::Attach(const Vec3& position, const Vec3& forward, const Vec3& up)
    {
        if (!_attached)
        {
            _lookYaw = 0;
            _lookPitch = 0;
        }
        _attached = true;
        _position = position;
        _attachForward = Normalize(forward);
        _attachUp = Normalize(up - _attachForward * Dot(_attachForward, up));
    }

    void Camera3D::Detach()
    {
        if (!_attached)
            return;
        const Vec3 forward = GetForward();
        _attached = false;
        SetOrientation(std::atan2(forward.y, forward.x), std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
    }

    void Camera3D::Update(const CameraInput& input, float deltaTime, const GroundHeightFunc& groundHeight)
    {
        if (_attached)
            return;

        deltaTime = std::clamp(deltaTime, 0.0f, 0.25f);

        Vec3 forward = GetForward();
        if (_mode == CameraMode::firstPerson)
        {
            // Walking moves along the ground plane regardless of where the camera looks.
            forward = Normalize(Vec3{ forward.x, forward.y, 0 });
        }
        const Vec3 right = GetRight();

        Vec3 move{};
        if (input.forward)
            move += forward;
        if (input.backward)
            move += -forward;
        if (input.right)
            move += right;
        if (input.left)
            move += -right;
        if (_mode == CameraMode::freeFly)
        {
            if (input.up)
                move += Vec3{ 0, 0, 1 };
            if (input.down)
                move += Vec3{ 0, 0, -1 };
        }

        float speed = _settings.moveSpeed * (input.sprint ? _settings.sprintMultiplier : 1.0f);
        if (_mode == CameraMode::firstPerson && _walk)
        {
            if (Length(move) > 0)
            {
                const Vec3 step = Normalize(move) * (speed * 0.35f * deltaTime);
                const Vec3 feet = _position - Vec3{ 0, 0, _settings.eyeHeight };
                // Try the full step, then slide along whichever axis is free.
                const Vec3 attempts[] = { step, { step.x, 0, 0 }, { 0, step.y, 0 } };
                for (const auto& attempt : attempts)
                {
                    if (Length(attempt) < 1e-4f)
                        continue;
                    float groundZ{};
                    if (_walk(feet, feet.x + attempt.x, feet.y + attempt.y, groundZ))
                    {
                        _position = { feet.x + attempt.x, feet.y + attempt.y, groundZ + _settings.eyeHeight };
                        break;
                    }
                }
            }
            _position.x = std::clamp(_position.x, _bounds.minX, _bounds.maxX);
            _position.y = std::clamp(_position.y, _bounds.minY, _bounds.maxY);
            return;
        }

        if (Length(move) > 0)
        {
            if (_mode == CameraMode::firstPerson)
                speed *= 0.35f;
            _position += Normalize(move) * (speed * deltaTime);
        }

        ApplyConstraints(groundHeight);
    }

    void Camera3D::ApplyConstraints(const GroundHeightFunc& groundHeight)
    {
        if (_attached)
            return;

        _position.x = std::clamp(_position.x, _bounds.minX, _bounds.maxX);
        _position.y = std::clamp(_position.y, _bounds.minY, _bounds.maxY);

        const float ground = groundHeight ? groundHeight(_position.x, _position.y) : 0.0f;
        if (_mode == CameraMode::firstPerson)
        {
            // Land on the highest walkable surface below the camera (a bridge, a path, or the terrain).
            float walkGround{};
            if (_walk && _walk(_position, _position.x, _position.y, walkGround))
                _position.z = walkGround + _settings.eyeHeight;
            else
                _position.z = ground + _settings.eyeHeight;
        }
        else
        {
            _position.z = std::clamp(_position.z, ground + kGroundClearance, std::max(ground + kGroundClearance, _bounds.maxZ));
        }
    }

    void Camera3D::LookAtTarget(const Vec3& target, float distance)
    {
        _position = target - GetForward() * distance;
    }

    Mat4 Camera3D::GetViewMatrix() const
    {
        return LookAt(_position, _position + GetForward(), GetUp());
    }

    Mat4 Camera3D::GetProjectionMatrix(float aspect, float zNear, float zFar) const
    {
        const float fov = std::clamp(_settings.fieldOfViewDegrees, 20.0f, 120.0f) * (kPi / 180.0f);
        return Perspective(fov, aspect, zNear, zFar);
    }
} // namespace OpenRCT2::Render3D
