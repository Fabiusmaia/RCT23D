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
#include <vector>

enum class VehiclePitch : uint8_t;
enum class VehicleRoll : uint8_t;
enum class VehicleTrackSubposition : uint8_t;

namespace OpenRCT2
{
    struct Ride;
    struct TrackElement;
    struct Vehicle;
    struct VehicleInfo;
    enum class TrackElemType : uint16_t;
} // namespace OpenRCT2

namespace OpenRCT2::Render3D
{
    // How a ride's track is rebuilt as 3D geometry.
    enum class TrackMeshStyle : uint8_t
    {
        // Not rebuilt: the game's sprites are used (flat rides, towers, shops, mazes, ...).
        none,
        // Two running rails on cross ties with a spine underneath (steel coasters, trains, monorails).
        rails,
        // Rails on a timber deck (wooden coasters).
        woodenRails,
        // A water trough (flumes, rapids, dinghy slides).
        channel,
        // A flat road (go-karts).
        road,
    };

    struct TrackRideStyle
    {
        TrackMeshStyle style = TrackMeshStyle::none;
        // The vehicles hang below the track (inverted and suspended coasters, suspended monorail).
        bool suspended{};
        // Height of the vehicle anchor above the track centreline, game z units.
        int32_t vehicleZOffset{};
    };

    TrackRideStyle GetTrackRideStyle(const Ride& ride);

    // Whether this track element is drawn as 3D geometry instead of the game's sprites. Stations keep their sprites
    // so that platforms and station buildings remain.
    bool IsTrackElementReconstructed(const TrackElement& element);

    // Position and orientation of a point on the track centreline, in 3D world space.
    struct TrackFrame
    {
        Vec3 position;
        Vec3 forward;
        Vec3 right;
        // Normal pointing out of the running surface, towards where an upright car sits.
        Vec3 up;
    };

    // Angle in radians for a bank; positive values bank to the right.
    float GetRollAngle(VehicleRoll roll);
    // Whether the vehicle is upside down at this pitch (top of loops, inverted sections).
    bool IsInvertedPitch(VehiclePitch pitch);

    // Builds a frame for every point of a track piece's vehicle path (from the same data the simulation moves
    // vehicles with). `origin` is the location of the piece's first element in game units.
    bool ComputeTrackFrames(
        VehicleTrackSubposition subposition, TrackElemType type, uint8_t direction, const CoordsXYZ& origin,
        std::vector<TrackFrame>& out);

    // Same as above, from raw path points. Exposed for unit tests.
    void ComputeTrackFrames(const VehicleInfo* points, size_t count, const CoordsXYZ& origin, std::vector<TrackFrame>& out);

    struct TrackColours
    {
        Colour4 main;
        Colour4 additional;
        Colour4 supports;
    };

    // Emits the track geometry along the frames.
    void AddTrackMesh(
        std::vector<Vertex>& out, const std::vector<TrackFrame>& frames, const TrackRideStyle& style,
        const TrackColours& colours);

    struct VehicleColours
    {
        Colour4 body;
        Colour4 trim;
        Colour4 riders;
    };

    // Where a vehicle is on its track and how it is oriented, from the simulation's own path data. `scratch` avoids
    // allocating per call. Returns false if the vehicle is not on a piece with path data.
    bool GetVehicleFrame(const Vehicle& vehicle, const TrackRideStyle& style, TrackFrame& out, std::vector<TrackFrame>& scratch);

    // The front seat rider's eyes for a car at the given frame (position, looking forward, up out of the seat).
    TrackFrame GetRiderView(const TrackFrame& frame, const TrackRideStyle& style);

    // Emits a car sitting on (or hanging from) the track at the given frame.
    void AddVehicleMesh(
        std::vector<Vertex>& out, const TrackFrame& frame, const TrackRideStyle& style, const VehicleColours& colours,
        int32_t riders);

    // An oriented box: centre plus half extents along forward/right/up.
    void AddOrientedBox(
        std::vector<Vertex>& out, const Vec3& centre, const Vec3& forward, const Vec3& right, const Vec3& up,
        const Vec3& halfExtents, Colour4 colour);
} // namespace OpenRCT2::Render3D
