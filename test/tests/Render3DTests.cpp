/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <cmath>
#include <gtest/gtest.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/render3d/Camera3D.h>
#include <openrct2/render3d/Math3D.h>
#include <openrct2/render3d/SceneBuilder.h>
#include <openrct2/render3d/SpriteBaker.h>
#include <openrct2/render3d/SpriteGeometry.h>
#include <openrct2/render3d/TrackGeometry.h>
#include <openrct2/ride/Angles.h>
#include <openrct2/ride/TrackData.h>
#include <openrct2/ride/VehicleSubpositionData.h>
#include <openrct2/ride/ted/TrackElementDescriptor.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/Slope.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Render3D;

namespace
{
    constexpr float kEpsilon = 1e-3f;

    void ExpectOrthonormal(const TrackFrame& f)
    {
        EXPECT_NEAR(Length(f.forward), 1.0f, kEpsilon);
        EXPECT_NEAR(Length(f.up), 1.0f, kEpsilon);
        EXPECT_NEAR(Length(f.right), 1.0f, kEpsilon);
        EXPECT_NEAR(Dot(f.forward, f.up), 0.0f, 1e-2f);
        EXPECT_NEAR(Dot(f.forward, f.right), 0.0f, 1e-2f);
        EXPECT_NEAR(Dot(f.up, f.right), 0.0f, 1e-2f);
    }

    // Converts a world space position back to game units.
    Vec3 WorldToGame(const Vec3& p)
    {
        return { p.x, p.y, WorldZToGame(p.z) };
    }

    TextureRegion MakeTexture()
    {
        TextureRegion t;
        t.u0 = 0.25f;
        t.v0 = 0.5f;
        t.u1 = 0.75f;
        t.v1 = 1.0f;
        t.layer = 3.0f;
        return t;
    }

    // Where in the sprite (0..1) a vertex samples, from its texture coordinates.
    std::pair<float, float> SpriteUV(const Vertex& v, const TextureRegion& t)
    {
        return { (v.u - t.u0) / (t.u1 - t.u0), (v.v - t.v0) / (t.v1 - t.v0) };
    }

    // Origin of the piece that follows a track piece, the way track designs are placed.
    CoordsXYZ NextPieceOrigin(TrackElemType type, const CoordsXYZ& origin, uint8_t& direction)
    {
        const auto& coords = TrackMetadata::GetTrackElementDescriptor(type).coordinates;
        CoordsXYZ next{ CoordsXY{ origin } + CoordsXY{ coords.x, coords.y }.rotate(direction),
                        origin.z - coords.zBegin + coords.zEnd };
        direction = static_cast<uint8_t>((direction + coords.rotationEnd - coords.rotationBegin) & 3);
        next += CoordsXYZ{ CoordsDirectionDelta[direction], 0 };
        return next;
    }
} // namespace

TEST(Render3D, GameToWorldScalesHeightOnly)
{
    const auto p = GameToWorld(64.0f, 32.0f, 16.0f);
    EXPECT_FLOAT_EQ(p.x, 64.0f);
    EXPECT_FLOAT_EQ(p.y, 32.0f);
    EXPECT_NEAR(p.z, 16.0f * kZScale, kEpsilon);
    EXPECT_NEAR(WorldZToGame(p.z), 16.0f, kEpsilon);
}

TEST(Render3D, ProjectIsometricMatchesGame)
{
    // The 3D view textures geometry by projecting it into the game's sprites, so this has to agree exactly.
    for (int32_t rotation = 0; rotation < 4; rotation++)
    {
        for (int32_t x = -64; x <= 96; x += 32)
        {
            for (int32_t y = -32; y <= 96; y += 16)
            {
                for (int32_t z = 0; z <= 48; z += 8)
                {
                    const auto expected = Translate3DTo2DWithZ(rotation, { x, y, z });
                    const auto actual = ProjectIsometric(
                        static_cast<uint8_t>(rotation), static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    EXPECT_FLOAT_EQ(actual.x, static_cast<float>(expected.x));
                    EXPECT_FLOAT_EQ(actual.y, static_cast<float>(expected.y));
                }
            }
        }
    }
}

TEST(Render3D, PerspectiveProjectsForwardToCentre)
{
    const Vec3 eye{ 10, 20, 30 };
    const Vec3 target{ 110, 20, 30 };
    const Mat4 vp = Perspective(70.0f * kPi / 180.0f, 1.5f, 1.0f, 1000.0f) * LookAt(eye, target, { 0, 0, 1 });
    const auto clip = vp.Transform(target);
    ASSERT_GT(clip[3], 0.0f);
    EXPECT_NEAR(clip[0] / clip[3], 0.0f, kEpsilon);
    EXPECT_NEAR(clip[1] / clip[3], 0.0f, kEpsilon);

    const Frustum frustum(vp);
    EXPECT_TRUE(frustum.Contains(target));
    EXPECT_FALSE(frustum.Contains({ -90, 20, 30 }));
    EXPECT_TRUE(frustum.Intersects({ { 100, 10, 20 }, { 120, 30, 40 } }));
    EXPECT_FALSE(frustum.Intersects({ { -200, 10, 20 }, { -150, 30, 40 } }));
}

TEST(Render3D, CameraStaysAboveGround)
{
    Camera3D camera;
    camera.SetBounds({ 0, 0, 1000, 1000, 500 });
    camera.SetPosition({ 100, 100, 10 });
    camera.ApplyConstraints([](float, float) { return 50.0f; });
    EXPECT_GE(camera.GetPosition().z, 50.0f);

    camera.SetPosition({ -500, 2000, 100 });
    camera.ApplyConstraints([](float, float) { return 0.0f; });
    EXPECT_GE(camera.GetPosition().x, 0.0f);
    EXPECT_LE(camera.GetPosition().y, 1000.0f);
}

TEST(Render3D, CameraMovesWhereItLooks)
{
    Camera3D camera;
    camera.SetBounds({ -1e6f, -1e6f, 1e6f, 1e6f, 1e6f });
    camera.SetPosition({ 0, 0, 100 });
    camera.SetOrientation(0.0f, 0.0f);
    EXPECT_NEAR(camera.GetForward().x, 1.0f, kEpsilon);
    EXPECT_NEAR(Dot(camera.GetForward(), camera.GetRight()), 0.0f, kEpsilon);

    CameraInput input;
    input.forward = true;
    camera.Update(input, 0.5f, [](float, float) { return 0.0f; });
    EXPECT_GT(camera.GetPosition().x, 1.0f);
    EXPECT_NEAR(camera.GetPosition().y, 0.0f, kEpsilon);
}

TEST(Render3D, TerrainSplitsAlongRaisedCorner)
{
    // A single raised corner gets its own triangle, so the fold runs between its two neighbours.
    TileCornersZ northUp{ 16, 0, 0, 0 };
    EXPECT_TRUE(SplitAlongEastWest(northUp));
    TileCornersZ eastUp{ 0, 16, 0, 0 };
    EXPECT_FALSE(SplitAlongEastWest(eastUp));

    // Slopes made from the game's own slope table must produce the same corner heights for neighbours.
    const auto flat = GetSlopeCornerHeights(32, kTileSlopeFlat);
    EXPECT_EQ(flat.north, 32);
    EXPECT_EQ(flat.south, 32);
}

TEST(Render3D, FlatSpriteCoversItsPixelsOnThePlane)
{
    const auto texture = MakeTexture();
    SpritePlacement placement;
    placement.left = -32;
    placement.top = -10;
    placement.width = 64;
    placement.height = 32;
    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        placement.rotation = rotation;
        std::vector<Vertex> out;
        const GroundPlane plane{ 40.0f, 0.0f, 0.0f };
        AddSpriteOnPlane(out, placement, plane, texture);
        ASSERT_EQ(out.size(), 6u);
        for (const auto& v : out)
        {
            const Vec3 g = WorldToGame(v.position);
            EXPECT_NEAR(g.z, 40.0f, 1e-2f);
            const auto screen = ProjectIsometric(rotation, g.x, g.y, g.z);
            const auto [u, t] = SpriteUV(v, texture);
            EXPECT_NEAR(screen.x, placement.left + u * placement.width, 1e-2f);
            EXPECT_NEAR(screen.y, placement.top + t * placement.height, 1e-2f);
            EXPECT_EQ(v.layer, texture.layer);
            EXPECT_EQ(v.flags, kVertexFlagSprite);
        }
    }
}

TEST(Render3D, SlopedPlaneFollowsGradient)
{
    const auto texture = MakeTexture();
    SpritePlacement placement;
    placement.left = -32;
    placement.top = -20;
    placement.width = 64;
    placement.height = 40;
    // Rises half a unit per unit along +x, like a sloped path.
    const GroundPlane plane{ 10.0f, 0.5f, 0.0f };
    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        placement.rotation = rotation;
        std::vector<Vertex> out;
        AddSpriteOnPlane(out, placement, plane, texture);
        ASSERT_EQ(out.size(), 6u);
        for (const auto& v : out)
        {
            const Vec3 g = WorldToGame(v.position);
            EXPECT_NEAR(g.z, plane.baseZ + plane.gradientX * g.x, 1e-2f);
        }
    }
}

TEST(Render3D, SpriteBoxTexturesMatchTheIsometricView)
{
    const auto texture = MakeTexture();
    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        // A small box with a much taller sprite (a tree) and a box that fits its sprite (a stall).
        for (const bool tall : { true, false })
        {
            SpritePlacement placement;
            placement.rotation = rotation;
            placement.boundsMin = { 100, 200, 16 };
            placement.boundsMax = tall ? Vec3{ 104, 204, 20 } : Vec3{ 124, 224, 40 };
            const auto base = ProjectIsometric(rotation, 112, 212, 16);
            placement.left = base.x - 30;
            placement.top = base.y - (tall ? 90 : 50);
            placement.width = 60;
            placement.height = tall ? 96 : 60;

            std::vector<Vertex> out;
            AddSpriteBox(out, placement, texture);
            ASSERT_FALSE(out.empty());
            ASSERT_EQ(out.size() % 3, 0u);

            for (const auto& v : out)
            {
                const Vec3 g = WorldToGame(v.position);
                const auto screen = ProjectIsometric(rotation, g.x, g.y, g.z);
                const auto [u, t] = SpriteUV(v, texture);
                EXPECT_NEAR(screen.x, placement.left + u * placement.width, 1e-2f);
                EXPECT_NEAR(screen.y, placement.top + t * placement.height, 1e-2f);
                // Geometry never goes below the object's base.
                EXPECT_GE(g.z, placement.boundsMin.z - 1e-2f);
                // The shader clips to the sprite's own rectangle in the atlas.
                EXPECT_EQ(v.offsetX, texture.u0);
                EXPECT_EQ(v.offsetY, texture.v0);
                EXPECT_EQ(v.normal.x, texture.u1);
                EXPECT_EQ(v.normal.y, texture.v1);
            }

            // Every face points towards the isometric camera of this rotation.
            float camX, camY;
            RotateXY(static_cast<uint8_t>((4 - rotation) & 3), 1.0f, 1.0f, camX, camY);
            const Vec3 towardsCamera = Normalize(Vec3{ camX, camY, 1.0f });
            for (size_t i = 0; i < out.size(); i += 3)
            {
                const Vec3 n = Cross(out[i + 1].position - out[i].position, out[i + 2].position - out[i].position);
                if (Length(n) < 1e-4f)
                    continue;
                EXPECT_GT(Dot(n, towardsCamera), 0.0f);
            }
        }
    }
}

TEST(Render3D, MismatchedSpriteFallsBackToCard)
{
    const auto texture = MakeTexture();
    SpritePlacement placement;
    placement.boundsMin = { 100, 100, 0 };
    placement.boundsMax = { 132, 132, 16 };
    placement.width = 40;
    placement.height = 40;
    // Drawn 200 pixels above where its box is.
    const auto base = ProjectIsometric(0, 116, 116, 0);
    placement.left = base.x - 20;
    placement.top = base.y - 240;
    EXPECT_TRUE(IsSpriteBoxMismatched(placement));

    // A sprite sitting on its box is fine.
    placement.top = base.y - 40;
    EXPECT_FALSE(IsSpriteBoxMismatched(placement));

    for (uint8_t rotation = 0; rotation < 4; rotation++)
    {
        placement.rotation = rotation;
        std::vector<Vertex> out;
        AddSpriteCard(out, placement, texture);
        ASSERT_EQ(out.size(), 6u);
        for (const auto& v : out)
        {
            const Vec3 g = WorldToGame(v.position);
            const auto screen = ProjectIsometric(rotation, g.x, g.y, g.z);
            const auto [u, t] = SpriteUV(v, texture);
            EXPECT_NEAR(screen.x, placement.left + u * placement.width, 1e-2f);
            EXPECT_NEAR(screen.y, placement.top + t * placement.height, 1e-2f);
        }
    }
}

TEST(Render3D, FlatTrackFramesFollowDirection)
{
    std::vector<TrackFrame> frames;
    static constexpr float kExpected[4][2] = { { -1, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 } };
    for (uint8_t direction = 0; direction < 4; direction++)
    {
        ASSERT_TRUE(
            ComputeTrackFrames(VehicleTrackSubposition::standard, TrackElemType::flat, direction, { 320, 320, 48 }, frames));
        ASSERT_GE(frames.size(), 2u);
        for (const auto& f : frames)
        {
            ExpectOrthonormal(f);
            EXPECT_NEAR(f.forward.x, kExpected[direction][0], kEpsilon);
            EXPECT_NEAR(f.forward.y, kExpected[direction][1], kEpsilon);
            EXPECT_NEAR(f.up.z, 1.0f, kEpsilon);
            EXPECT_NEAR(WorldZToGame(f.position.z), 48.0f, kEpsilon);
            // Within the piece's tile.
            EXPECT_GE(f.position.x, 320.0f);
            EXPECT_LE(f.position.x, 352.0f);
        }
    }
}

TEST(Render3D, LoopTurnsTrackUpsideDown)
{
    std::vector<TrackFrame> frames;
    ASSERT_TRUE(
        ComputeTrackFrames(VehicleTrackSubposition::standard, TrackElemType::leftVerticalLoop, 0, { 0, 0, 0 }, frames));
    ASSERT_GT(frames.size(), 10u);

    size_t top = 0;
    for (size_t i = 0; i < frames.size(); i++)
    {
        ExpectOrthonormal(frames[i]);
        if (frames[i].position.z > frames[top].position.z)
            top = i;
    }
    EXPECT_GT(frames.front().up.z, 0.9f);
    EXPECT_GT(frames.back().up.z, 0.9f);
    // At the top of the loop the running surface faces down.
    EXPECT_LT(frames[top].up.z, -0.9f);
    EXPECT_GT(WorldZToGame(frames[top].position.z), 32.0f);
}

TEST(Render3D, BankedTurnTiltsTrack)
{
    std::vector<TrackFrame> frames;
    ASSERT_TRUE(ComputeTrackFrames(
        VehicleTrackSubposition::standard, TrackElemType::bankedLeftQuarterTurn5Tiles, 0, { 0, 0, 0 }, frames));
    const auto& middle = frames[frames.size() / 2];
    ExpectOrthonormal(middle);
    EXPECT_LT(middle.up.z, 0.95f);
    EXPECT_GT(middle.up.z, 0.5f);
}

TEST(Render3D, ConnectedTrackPiecesMeet)
{
    // flat -> flat to 25 deg up -> 25 deg up -> 25 deg up to flat -> flat, in every direction.
    static constexpr TrackElemType kPieces[] = {
        TrackElemType::flat, TrackElemType::flatToUp25, TrackElemType::up25, TrackElemType::up25ToFlat,
        TrackElemType::leftQuarterTurn5Tiles, TrackElemType::flat,
    };
    for (uint8_t startDirection = 0; startDirection < 4; startDirection++)
    {
        uint8_t direction = startDirection;
        CoordsXYZ origin{ 1024, 1024, 64 };
        std::vector<TrackFrame> previous;
        for (const auto type : kPieces)
        {
            std::vector<TrackFrame> frames;
            ASSERT_TRUE(ComputeTrackFrames(VehicleTrackSubposition::standard, type, direction, origin, frames));
            if (!previous.empty())
            {
                const float gap = Length(frames.front().position - previous.back().position);
                EXPECT_LT(gap, 2.0f) << "piece " << static_cast<int>(type) << " direction " << static_cast<int>(direction);
                // No kink where they meet.
                EXPECT_GT(Dot(frames.front().forward, previous.back().forward), 0.9f);
                EXPECT_GT(Dot(frames.front().up, previous.back().up), 0.9f);
            }
            previous = frames;
            origin = NextPieceOrigin(type, origin, direction);
        }
    }
}

TEST(Render3D, TrackMeshIsFinite)
{
    std::vector<TrackFrame> frames;
    ASSERT_TRUE(
        ComputeTrackFrames(VehicleTrackSubposition::standard, TrackElemType::leftVerticalLoop, 1, { 64, 64, 32 }, frames));
    const TrackColours colours{ { 200, 0, 0, 255 }, { 0, 0, 200, 255 }, { 80, 80, 80, 255 } };
    for (const auto meshStyle :
         { TrackMeshStyle::rails, TrackMeshStyle::woodenRails, TrackMeshStyle::channel, TrackMeshStyle::road })
    {
        for (const bool suspended : { false, true })
        {
            TrackRideStyle style;
            style.style = meshStyle;
            style.suspended = suspended;
            style.vehicleZOffset = 8;
            std::vector<Vertex> out;
            AddTrackMesh(out, frames, style, colours);
            ASSERT_FALSE(out.empty());
            EXPECT_EQ(out.size() % 3, 0u);
            for (const auto& v : out)
            {
                EXPECT_TRUE(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
                EXPECT_NEAR(Length(v.normal), 1.0f, 1e-2f);
            }

            std::vector<Vertex> car;
            AddVehicleMesh(car, frames[frames.size() / 2], style, { colours.main, colours.additional, colours.supports }, 4);
            EXPECT_FALSE(car.empty());
        }
    }

    // No style, no geometry.
    std::vector<Vertex> none;
    AddTrackMesh(none, frames, TrackRideStyle{}, colours);
    EXPECT_TRUE(none.empty());
}

TEST(Render3D, Scale2xKeepsColoursAndSmoothsDiagonals)
{
    // A 3x3 image: a diagonal line of colour 5 on transparent (0).
    IndexedImage image;
    image.width = 3;
    image.height = 3;
    image.originX = 1;
    image.originY = 2;
    image.pixels = { 5, 0, 0, 0, 5, 0, 0, 0, 5 };

    const auto scaled = Scale2x(image);
    ASSERT_EQ(scaled.width, 6);
    ASSERT_EQ(scaled.height, 6);
    EXPECT_EQ(scaled.originX, 2);
    EXPECT_EQ(scaled.originY, 4);
    for (const auto pixel : scaled.pixels)
        EXPECT_TRUE(pixel == 0 || pixel == 5);

    // The centre pixel of the diagonal grows towards its neighbours instead of staying a 2x2 block: its top-left and
    // bottom-right sub-pixels stay set, and the steps between the diagonal pixels are filled in.
    EXPECT_EQ(scaled.At(2, 2), 5);
    EXPECT_EQ(scaled.At(3, 3), 5);
    EXPECT_EQ(scaled.At(1, 2), 5);
    EXPECT_EQ(scaled.At(2, 1), 5);

    // Flat areas stay flat.
    IndexedImage flat;
    flat.width = 2;
    flat.height = 2;
    flat.pixels = { 7, 7, 7, 7 };
    for (const auto pixel : Scale2x(flat).pixels)
        EXPECT_EQ(pixel, 7);
}

TEST(Render3D, RollAngles)
{
    EXPECT_FLOAT_EQ(GetRollAngle(VehicleRoll::unbanked), 0.0f);
    EXPECT_LT(GetRollAngle(VehicleRoll::left45), 0.0f);
    EXPECT_GT(GetRollAngle(VehicleRoll::right45), 0.0f);
    EXPECT_NEAR(GetRollAngle(VehicleRoll::right90), kPi / 2.0f, kEpsilon);
    EXPECT_TRUE(IsInvertedPitch(VehiclePitch::inverted));
    EXPECT_FALSE(IsInvertedPitch(VehiclePitch::up25));
}
