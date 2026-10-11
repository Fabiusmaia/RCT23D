/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TrackGeometry.h"

#include "../ride/Angles.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/TrackStyle.h"
#include "../ride/Vehicle.h"
#include "../ride/VehicleSubpositionData.h"
#include "../world/tile_element/TrackElement.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace OpenRCT2::Render3D
{
    namespace
    {
        constexpr float kDegrees = kPi / 180.0f;

        // Sizes in world units (1/32 of a tile horizontally).
        constexpr float kRailGauge = 3.2f;
        constexpr float kRailRadius = 0.8f;
        constexpr float kRailHeight = 1.5f;
        constexpr float kSpineDepth = 2.6f;
        constexpr float kSpineRadius = 1.3f;
        constexpr float kTieSpacing = 7.0f;
        constexpr float kMaxSegmentLength = 8.0f;
        constexpr float kMaxSegmentAngleCos = 0.985f; // about 10 degrees

        void PushVertex(std::vector<Vertex>& out, const Vec3& p, const Vec3& n, Colour4 c)
        {
            Vertex v;
            v.position = p;
            v.normal = n;
            v.r = c.r;
            v.g = c.g;
            v.b = c.b;
            v.a = c.a;
            out.push_back(v);
        }

        // Emits a quad wound so that it faces `outward`.
        void AddQuadFacing(
            std::vector<Vertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& outward,
            Colour4 colour)
        {
            const Vec3 n = Normalize(outward);
            if (Dot(Cross(b - a, c - a), outward) >= 0)
            {
                PushVertex(out, a, n, colour);
                PushVertex(out, b, n, colour);
                PushVertex(out, c, n, colour);
                PushVertex(out, a, n, colour);
                PushVertex(out, c, n, colour);
                PushVertex(out, d, n, colour);
            }
            else
            {
                PushVertex(out, a, n, colour);
                PushVertex(out, c, n, colour);
                PushVertex(out, b, n, colour);
                PushVertex(out, a, n, colour);
                PushVertex(out, d, n, colour);
                PushVertex(out, c, n, colour);
            }
        }

        // The side of the track the cars are on: up for normal track, down for suspended track.
        Vec3 CarSide(const TrackFrame& f, const TrackRideStyle& style)
        {
            return style.suspended ? -f.up : f.up;
        }

        // Centre of the running rails.
        Vec3 RailCentre(const TrackFrame& f, const TrackRideStyle& style)
        {
            if (style.suspended)
            {
                // Suspended cars hang from track mounted above their anchor point.
                return f.position + f.up * ((static_cast<float>(style.vehicleZOffset) + 4.0f) * kZScale);
            }
            return f.position + f.up * kRailHeight;
        }

        // A square tube following the frames, offset from the rail centre.
        void AddTube(
            std::vector<Vertex>& out, const std::vector<TrackFrame>& frames, const TrackRideStyle& style, float offsetRight,
            float offsetCarSide, float radius, Colour4 colour)
        {
            static constexpr std::array<float, 4> kCos = { 0.70710678f, -0.70710678f, -0.70710678f, 0.70710678f };
            static constexpr std::array<float, 4> kSin = { 0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f };

            auto corner = [&](const TrackFrame& f, size_t k) {
                const Vec3 side = CarSide(f, style);
                const Vec3 centre = RailCentre(f, style) + f.right * offsetRight + side * offsetCarSide;
                return centre + (f.right * kCos[k] + side * kSin[k]) * radius;
            };

            for (size_t i = 0; i + 1 < frames.size(); i++)
            {
                const auto& a = frames[i];
                const auto& b = frames[i + 1];
                for (size_t k = 0; k < 4; k++)
                {
                    const size_t k2 = (k + 1) % 4;
                    const Vec3 a0 = corner(a, k);
                    const Vec3 a1 = corner(a, k2);
                    const Vec3 b0 = corner(b, k);
                    const Vec3 b1 = corner(b, k2);
                    const Vec3 side = CarSide(a, style);
                    const Vec3 outward = a.right * (kCos[k] + kCos[k2]) + side * (kSin[k] + kSin[k2]);
                    AddQuadFacing(out, a0, b0, b1, a1, outward, colour);
                }
            }
        }

        // A flat strip across the track, visible from both sides.
        void AddRibbon(
            std::vector<Vertex>& out, const std::vector<TrackFrame>& frames, const TrackRideStyle& style, float width,
            float offsetCarSide, Colour4 top, Colour4 bottom)
        {
            const float half = width * 0.5f;
            for (size_t i = 0; i + 1 < frames.size(); i++)
            {
                const auto& a = frames[i];
                const auto& b = frames[i + 1];
                const Vec3 sideA = CarSide(a, style);
                const Vec3 sideB = CarSide(b, style);
                const Vec3 ca = RailCentre(a, style) + sideA * offsetCarSide;
                const Vec3 cb = RailCentre(b, style) + sideB * offsetCarSide;
                const Vec3 al = ca - a.right * half;
                const Vec3 ar = ca + a.right * half;
                const Vec3 bl = cb - b.right * half;
                const Vec3 br = cb + b.right * half;
                AddQuadFacing(out, al, ar, br, bl, sideA, top);
                AddQuadFacing(out, al, ar, br, bl, -sideA, bottom);
            }
        }

        // A wall along one edge of the track, visible from both sides.
        void AddSideWall(
            std::vector<Vertex>& out, const std::vector<TrackFrame>& frames, const TrackRideStyle& style, float offsetRight,
            float height, Colour4 colour)
        {
            for (size_t i = 0; i + 1 < frames.size(); i++)
            {
                const auto& a = frames[i];
                const auto& b = frames[i + 1];
                const Vec3 a0 = RailCentre(a, style) + a.right * offsetRight;
                const Vec3 b0 = RailCentre(b, style) + b.right * offsetRight;
                const Vec3 a1 = a0 + CarSide(a, style) * height;
                const Vec3 b1 = b0 + CarSide(b, style) * height;
                AddQuadFacing(out, a0, b0, b1, a1, a.right, colour);
                AddQuadFacing(out, a0, b0, b1, a1, -a.right, colour);
            }
        }

        // Picks a subset of frames that keeps curves smooth without emitting a segment per path point.
        std::vector<TrackFrame> SelectFrames(const std::vector<TrackFrame>& frames)
        {
            std::vector<TrackFrame> result;
            if (frames.empty())
                return result;
            result.push_back(frames.front());
            for (size_t i = 1; i < frames.size(); i++)
            {
                const auto& last = result.back();
                const auto& f = frames[i];
                const bool isLast = i + 1 == frames.size();
                if (isLast || Length(f.position - last.position) >= kMaxSegmentLength
                    || Dot(f.forward, last.forward) < kMaxSegmentAngleCos || Dot(f.up, last.up) < kMaxSegmentAngleCos)
                {
                    result.push_back(f);
                }
            }
            return result;
        }

        void AddTies(
            std::vector<Vertex>& out, const std::vector<TrackFrame>& frames, const TrackRideStyle& style, float halfWidth,
            float offsetCarSide, Colour4 colour)
        {
            float travelled = kTieSpacing * 0.5f;
            for (size_t i = 1; i < frames.size(); i++)
            {
                travelled += Length(frames[i].position - frames[i - 1].position);
                if (travelled < kTieSpacing)
                    continue;
                travelled = 0;
                const auto& f = frames[i];
                const Vec3 side = CarSide(f, style);
                const Vec3 centre = RailCentre(f, style) + side * offsetCarSide;
                // A slab without its two small end faces, which are hidden by the rails anyway.
                const Vec3 df = f.forward * 0.6f;
                const Vec3 dr = f.right * halfWidth;
                const Vec3 du = side * 0.45f;
                auto p = [&](float sf, float sr, float su) { return centre + df * sf + dr * sr + du * su; };
                AddQuadFacing(out, p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1), side, colour);
                AddQuadFacing(out, p(-1, -1, -1), p(1, -1, -1), p(1, 1, -1), p(-1, 1, -1), -side, colour);
                AddQuadFacing(out, p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1), f.forward, colour);
                AddQuadFacing(out, p(-1, -1, -1), p(-1, 1, -1), p(-1, 1, 1), p(-1, -1, 1), -f.forward, colour);
            }
        }

        Colour4 Darken(Colour4 c, float f)
        {
            return { static_cast<uint8_t>(c.r * f), static_cast<uint8_t>(c.g * f), static_cast<uint8_t>(c.b * f), c.a };
        }
    } // namespace

    TrackRideStyle GetTrackRideStyle(const Ride& ride)
    {
        TrackRideStyle result;
        const auto& rtd = ride.getRideTypeDescriptor();
        if (!rtd.flags.has(RtdFlag::hasTrack) || rtd.flags.has(RtdFlag::isFlatRide)
            || rtd.flags.has(RtdFlag::isShopOrFacility) || rtd.flags.has(RtdFlag::noVehicles))
        {
            return result;
        }

        result.vehicleZOffset = rtd.Heights.VehicleZOffset;
        switch (getTrackDrawerEntry(rtd).trackStyle)
        {
            // Rides whose track is better shown by the game's own art: towers, lifts, water-only and walking rides.
            case TrackStyle::null:
            case TrackStyle::boatHire:
            case TrackStyle::submarineRide:
            case TrackStyle::chairlift:
            case TrackStyle::lift:
            case TrackStyle::launchedFreefall:
            case TrackStyle::rotoDrop:
            case TrackStyle::observationTower:
            case TrackStyle::maze:
            case TrackStyle::miniGolf:
            case TrackStyle::spiralSlide:
                result.style = TrackMeshStyle::none;
                break;
            case TrackStyle::classicWoodenRollerCoaster:
            case TrackStyle::classicWoodenTwisterRollerCoaster:
            case TrackStyle::woodenRollerCoaster:
            case TrackStyle::woodenWildMouse:
            case TrackStyle::sideFrictionRollerCoaster:
            case TrackStyle::virginiaReel:
                result.style = TrackMeshStyle::woodenRails;
                break;
            case TrackStyle::logFlume:
            case TrackStyle::riverRapids:
            case TrackStyle::splashBoats:
            case TrackStyle::dinghySlide:
            case TrackStyle::dinghySlideCovered:
                result.style = TrackMeshStyle::channel;
                break;
            case TrackStyle::goKarts:
                result.style = TrackMeshStyle::road;
                break;
            case TrackStyle::invertedRollerCoaster:
            case TrackStyle::invertedHairpinCoaster:
            case TrackStyle::invertedImpulseCoaster:
            case TrackStyle::compactInvertedCoaster:
            case TrackStyle::suspendedSwingingCoaster:
            case TrackStyle::miniSuspendedCoaster:
            case TrackStyle::suspendedMonorail:
                result.style = TrackMeshStyle::rails;
                result.suspended = true;
                break;
            default:
                result.style = TrackMeshStyle::rails;
                break;
        }
        return result;
    }

    bool IsTrackElementReconstructed(const TrackElement& element)
    {
        if (element.isStation())
            return false;
        const auto* ride = GetRide(element.getRideIndex());
        if (ride == nullptr)
            return false;
        if (GetTrackRideStyle(*ride).style == TrackMeshStyle::none)
            return false;
        // Pieces without a vehicle path (e.g. decorative or flat-ride parts) cannot be rebuilt.
        return VehicleGetMoveInfoSize(VehicleTrackSubposition::standard, element.getTrackType(), element.getDirection()) > 1;
    }

    float GetRollAngle(VehicleRoll roll)
    {
        switch (roll)
        {
            case VehicleRoll::left22:
            case VehicleRoll::uninvertingLeft22:
                return -22.5f * kDegrees;
            case VehicleRoll::left45:
            case VehicleRoll::uninvertingLeft45:
                return -45.0f * kDegrees;
            case VehicleRoll::left67:
                return -67.5f * kDegrees;
            case VehicleRoll::left90:
                return -90.0f * kDegrees;
            case VehicleRoll::left112:
                return -112.5f * kDegrees;
            case VehicleRoll::left135:
                return -135.0f * kDegrees;
            case VehicleRoll::left157:
                return -157.5f * kDegrees;
            case VehicleRoll::right22:
            case VehicleRoll::uninvertingRight22:
                return 22.5f * kDegrees;
            case VehicleRoll::right45:
            case VehicleRoll::uninvertingRight45:
                return 45.0f * kDegrees;
            case VehicleRoll::right67:
                return 67.5f * kDegrees;
            case VehicleRoll::right90:
                return 90.0f * kDegrees;
            case VehicleRoll::right112:
                return 112.5f * kDegrees;
            case VehicleRoll::right135:
                return 135.0f * kDegrees;
            case VehicleRoll::right157:
                return 157.5f * kDegrees;
            default:
                return 0.0f;
        }
    }

    bool IsInvertedPitch(VehiclePitch pitch)
    {
        switch (pitch)
        {
            case VehiclePitch::up105:
            case VehiclePitch::up120:
            case VehiclePitch::up135:
            case VehiclePitch::up150:
            case VehiclePitch::up165:
            case VehiclePitch::inverted:
            case VehiclePitch::down105:
            case VehiclePitch::down120:
            case VehiclePitch::down135:
            case VehiclePitch::down150:
            case VehiclePitch::down165:
            case VehiclePitch::corkscrewUpRight2:
            case VehiclePitch::corkscrewDownLeft2:
            case VehiclePitch::corkscrewUpLeft2:
            case VehiclePitch::corkscrewDownRight2:
                return true;
            default:
                return false;
        }
    }

    void ComputeTrackFrames(const VehicleInfo* points, size_t count, const CoordsXYZ& origin, std::vector<TrackFrame>& out)
    {
        out.clear();
        if (points == nullptr || count == 0)
            return;

        out.resize(count);
        for (size_t i = 0; i < count; i++)
        {
            const auto& p = points[i];
            out[i].position = GameToWorld(
                static_cast<float>(origin.x + p.x), static_cast<float>(origin.y + p.y), static_cast<float>(origin.z + p.z));
        }

        const Vec3 worldUp{ 0, 0, 1 };
        Vec3 unbankedUp{ 0, 0, 1 };
        for (size_t i = 0; i < count; i++)
        {
            // Path points lie on a 1 unit grid, so a wider window keeps the stair-stepping out of the tangent.
            const size_t prev = i >= 3 ? i - 3 : 0;
            const size_t next = std::min(count - 1, i + 3);
            Vec3 forward = Normalize(out[next].position - out[prev].position);
            if (Length(forward) < 0.5f)
            {
                // Stationary points: fall back to the sprite heading. Yaw 0 travels towards -x, 8 towards +y.
                const float angle = kPi - static_cast<float>(points[i].yaw) * (kPi / 16.0f);
                forward = { std::cos(angle), std::sin(angle), 0.0f };
            }

            // Keep "up" continuous along the path (this turns it over through loops), but re-align it with the
            // world vertical wherever the track is clearly not steep, so it never drifts.
            const Vec3 projected = worldUp - forward * Dot(forward, worldUp);
            const float projectedLength = Length(projected);
            if (i == 0)
            {
                if (projectedLength > 0.2f)
                    unbankedUp = projected * (1.0f / projectedLength);
                else
                    unbankedUp = Normalize(Cross(Cross(forward, Vec3{ 1, 0, 0 }), forward));
                if (IsInvertedPitch(points[0].pitch))
                    unbankedUp = -unbankedUp;
            }
            else
            {
                Vec3 transported = unbankedUp - forward * Dot(forward, unbankedUp);
                if (Length(transported) > 1e-3f)
                    transported = Normalize(transported);
                else
                    transported = unbankedUp;

                unbankedUp = transported;
                if (projectedLength > 0.5f)
                {
                    const Vec3 aligned = projected * (1.0f / projectedLength);
                    const float agreement = Dot(aligned, transported);
                    if (std::abs(agreement) > 0.3f)
                        unbankedUp = agreement >= 0 ? aligned : -aligned;
                }
            }

            const Vec3 unbankedRight = Normalize(Cross(forward, unbankedUp));
            const float roll = GetRollAngle(points[i].roll);
            const Vec3 up = Normalize(unbankedUp * std::cos(roll) + unbankedRight * std::sin(roll));

            out[i].forward = forward;
            out[i].up = up;
            out[i].right = Normalize(Cross(forward, up));
        }
    }

    bool ComputeTrackFrames(
        VehicleTrackSubposition subposition, TrackElemType type, uint8_t direction, const CoordsXYZ& origin,
        std::vector<TrackFrame>& out)
    {
        out.clear();
        const auto size = VehicleGetMoveInfoSize(subposition, type, direction);
        if (size == 0)
            return false;

        const uint16_t typeAndDirection = static_cast<uint16_t>((EnumValue(type) << 2) | (direction & 3));
        const auto* list = gTrackVehicleInfo[EnumValue(subposition)][typeAndDirection];
        if (list == nullptr || list->info == nullptr)
            return false;

        ComputeTrackFrames(list->info, std::min<size_t>(size, list->size), origin, out);
        return !out.empty();
    }

    void AddOrientedBox(
        std::vector<Vertex>& out, const Vec3& centre, const Vec3& forward, const Vec3& right, const Vec3& up,
        const Vec3& halfExtents, Colour4 colour)
    {
        const Vec3 f = forward * halfExtents.x;
        const Vec3 r = right * halfExtents.y;
        const Vec3 u = up * halfExtents.z;
        auto p = [&](float sf, float sr, float su) { return centre + f * sf + r * sr + u * su; };

        AddQuadFacing(out, p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1), up, colour);
        AddQuadFacing(out, p(-1, -1, -1), p(1, -1, -1), p(1, 1, -1), p(-1, 1, -1), -up, Darken(colour, 0.8f));
        AddQuadFacing(out, p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1), forward, colour);
        AddQuadFacing(out, p(-1, -1, -1), p(-1, 1, -1), p(-1, 1, 1), p(-1, -1, 1), -forward, colour);
        AddQuadFacing(out, p(-1, 1, -1), p(1, 1, -1), p(1, 1, 1), p(-1, 1, 1), right, colour);
        AddQuadFacing(out, p(-1, -1, -1), p(1, -1, -1), p(1, -1, 1), p(-1, -1, 1), -right, colour);
    }

    void AddTrackMesh(
        std::vector<Vertex>& out, const std::vector<TrackFrame>& allFrames, const TrackRideStyle& style,
        const TrackColours& colours)
    {
        if (allFrames.size() < 2)
            return;
        const auto frames = SelectFrames(allFrames);

        switch (style.style)
        {
            case TrackMeshStyle::none:
                break;
            case TrackMeshStyle::rails:
                AddTube(out, frames, style, -kRailGauge, 0.0f, kRailRadius, colours.main);
                AddTube(out, frames, style, kRailGauge, 0.0f, kRailRadius, colours.main);
                AddTube(out, frames, style, 0.0f, -kSpineDepth, kSpineRadius, colours.additional);
                AddTies(out, allFrames, style, kRailGauge, -0.9f, colours.additional);
                break;
            case TrackMeshStyle::woodenRails:
                AddTube(out, frames, style, -kRailGauge, 0.0f, kRailRadius, colours.main);
                AddTube(out, frames, style, kRailGauge, 0.0f, kRailRadius, colours.main);
                AddRibbon(out, frames, style, kRailGauge * 2.0f + 4.0f, -1.0f, colours.additional, Darken(colours.additional, 0.7f));
                AddTies(out, allFrames, style, kRailGauge + 1.5f, -1.3f, Darken(colours.additional, 0.85f));
                break;
            case TrackMeshStyle::channel:
            {
                constexpr float kHalfWidth = 7.0f;
                AddRibbon(out, frames, style, kHalfWidth * 2.0f, -1.0f, colours.main, Darken(colours.main, 0.7f));
                AddSideWall(out, frames, style, -kHalfWidth, 5.0f, colours.additional);
                AddSideWall(out, frames, style, kHalfWidth, 5.0f, colours.additional);
                AddRibbon(out, frames, style, kHalfWidth * 2.0f - 0.5f, 1.0f, { 70, 120, 200, 255 }, { 70, 120, 200, 255 });
                break;
            }
            case TrackMeshStyle::road:
                AddRibbon(out, frames, style, 18.0f, -0.5f, colours.main, Darken(colours.main, 0.7f));
                AddTube(out, frames, style, -9.0f, 0.0f, 0.9f, colours.additional);
                AddTube(out, frames, style, 9.0f, 0.0f, 0.9f, colours.additional);
                break;
        }
    }

    namespace
    {
        constexpr float kCarHalfLength = 5.0f;
        constexpr float kCarHalfHeight = 2.4f;

        // Centre of a car's body: on top of the rails, or hanging well below suspended track.
        Vec3 CarBodyCentre(const TrackFrame& frame, const TrackRideStyle& style)
        {
            const float gap = style.suspended ? 4.0f : 0.6f;
            return RailCentre(frame, style) + CarSide(frame, style) * (kRailRadius + gap + kCarHalfHeight);
        }
    } // namespace

    bool GetVehicleFrame(const Vehicle& vehicle, const TrackRideStyle& style, TrackFrame& out, std::vector<TrackFrame>& scratch)
    {
        if (!ComputeTrackFrames(
                vehicle.TrackSubposition, vehicle.GetTrackType(), vehicle.GetTrackDirection(), vehicle.TrackLocation, scratch))
            return false;

        // The simulation's own path position, so the car is exactly where the game says it is.
        const size_t index = std::min<size_t>(vehicle.track_progress, scratch.size() - 1);
        out = scratch[index];
        const auto location = vehicle.getLocation();
        out.position = GameToWorld(
            static_cast<float>(location.x), static_cast<float>(location.y),
            static_cast<float>(location.z - style.vehicleZOffset));
        return true;
    }

    TrackFrame GetRiderView(const TrackFrame& frame, const TrackRideStyle& style)
    {
        // Riders sit upright in the car (towards the track's up, also when hanging below suspended track). The eye is
        // in the back row, between the seats and a little above the heads, so the car's front stays a thin strip at the
        // bottom of the view instead of filling it.
        TrackFrame view = frame;
        view.position = CarBodyCentre(frame, style) + frame.up * (kCarHalfHeight + 3.4f) - frame.forward * 1.5f;
        return view;
    }

    void AddVehicleMesh(
        std::vector<Vertex>& out, const TrackFrame& frame, const TrackRideStyle& style, const VehicleColours& colours,
        int32_t riders)
    {
        const Vec3 side = CarSide(frame, style);
        const float halfWidth = style.style == TrackMeshStyle::road ? 3.5f : 4.0f;

        const Vec3 body = CarBodyCentre(frame, style);
        AddOrientedBox(out, body, frame.forward, frame.right, side, { kCarHalfLength, halfWidth, kCarHalfHeight }, colours.body);
        // A raised front in the trim colour.
        AddOrientedBox(
            out, body + frame.up * (kCarHalfHeight + 0.4f) + frame.forward * (kCarHalfLength - 1.2f), frame.forward,
            frame.right, frame.up, { 1.2f, halfWidth, 0.9f }, colours.trim);

        // Riders' heads, two abreast, sitting upright in the car.
        const int32_t count = std::clamp(riders, 0, 4);
        for (int32_t i = 0; i < count; i++)
        {
            const float along = (i / 2 == 0) ? 1.0f : -2.6f;
            const float across = (i % 2 == 0) ? -1.8f : 1.8f;
            const Vec3 head = body + frame.up * (kCarHalfHeight + 1.6f) + frame.forward * along + frame.right * across;
            AddOrientedBox(out, head, frame.forward, frame.right, frame.up, { 0.9f, 0.9f, 1.1f }, colours.riders);
        }
    }
} // namespace OpenRCT2::Render3D
