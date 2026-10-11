/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Render3D.h"

#include "../Context.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../config/Config.h"
#include "../drawing/Drawing.Screen.h"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/G1Element.h"
#include "../drawing/IDrawingEngine.h"
#include "../entity/EntityList.h"
#include "../interface/Viewport.h"
#include "../interface/Window.h"
#include "../interface/WindowBase.h"
#include "../localisation/Formatter.h"
#include "../localisation/StringIds.h"
#include "../ride/Ride.h"
#include "../ride/Vehicle.h"
#include "../world/Map.h"
#include "../world/tile_element/TileElement.h"
#include "PaintCapture.h"
#include "TrackGeometry.h"
#include "WalkCollision.h"

#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

namespace OpenRCT2::Render3D
{
    namespace
    {
        bool _active = false;
        Camera3D _camera;
        std::optional<std::string> _pendingFailure;
        std::optional<std::string> _pendingScreenshot;
        bool _statsRequested = false;
        std::optional<EntityId> _ridingVehicle;
        std::vector<TrackFrame> _rideFrames;

        // Vehicles further away than this (world units) are not picked for riding.
        constexpr float kRidePickDistance = 24.0f * kCoordsXYStep;
        std::chrono::steady_clock::time_point _lastUpdate{};
        bool _hasLastUpdate = false;

        bool IsSceneSupported()
        {
            return gLegacyScene == LegacyScene::playing || gLegacyScene == LegacyScene::scenarioEditor
                || gLegacyScene == LegacyScene::trackDesigner;
        }

        CameraBounds GetMapBounds()
        {
            const auto& mapSize = getGameState().mapSize;
            const float margin = 8.0f * kCoordsXYStep;
            return { -margin, -margin, mapSize.x * kCoordsXYStep + margin, mapSize.y * kCoordsXYStep + margin,
                     GameToWorld(0, 0, 4000).z };
        }

        // Places the camera above whatever the main isometric viewport is centred on, looking the same way.
        void PlaceCameraFromMainViewport()
        {
            auto* mainWindow = WindowGetMain();
            Viewport* viewport = mainWindow != nullptr ? mainWindow->viewport : nullptr;

            CoordsXY target = { getGameState().mapSize.x * kCoordsXYStep / 2, getGameState().mapSize.y * kCoordsXYStep / 2 };
            uint8_t rotation = 0;
            if (viewport != nullptr)
            {
                rotation = viewport->rotation & 3;
                Viewport* found = nullptr;
                const ScreenCoordsXY centre{ viewport->pos.x + viewport->width / 2, viewport->pos.y + viewport->height / 2 };
                if (auto mapPos = ScreenGetMapXY(centre, &found); mapPos.has_value())
                    target = *mapPos;
            }

            // Isometric view directions for rotations 0-3, see Translate3DTo2DWithZ.
            static constexpr float kViewYaw[] = { -0.75f * kPi, -0.25f * kPi, 0.25f * kPi, 0.75f * kPi };
            _camera.SetOrientation(kViewYaw[rotation], -0.55f);

            const float groundZ = GetGroundHeight(static_cast<float>(target.x), static_cast<float>(target.y));
            _camera.SetBounds(GetMapBounds());
            _camera.LookAtTarget({ static_cast<float>(target.x), static_cast<float>(target.y), groundZ }, 420.0f);
            _camera.ApplyConstraints(GetGroundHeight);
        }

        // Moves the isometric view to the point the 3D camera is looking at.
        void SyncMainViewportFromCamera()
        {
            auto* mainWindow = WindowGetMain();
            if (mainWindow == nullptr)
                return;

            const Vec3 pos = _camera.GetPosition();
            const Vec3 forward = _camera.GetForward();
            Vec3 target = pos;
            // March along the view ray until it hits the ground, so the 2D view centres on what was being looked at.
            for (float t = 0; t < 4000.0f; t += 16.0f)
            {
                const Vec3 p = pos + forward * t;
                if (p.z <= GetGroundHeight(p.x, p.y))
                {
                    target = p;
                    break;
                }
            }
            const CoordsXYZ coords{ static_cast<int32_t>(target.x), static_cast<int32_t>(target.y),
                                    static_cast<int32_t>(WorldZToGame(target.z)) };
            if (MapIsLocationValid(coords))
                WindowScrollToLocation(*mainWindow, coords);
        }

        void ShowPendingFailure()
        {
            if (!_pendingFailure.has_value())
                return;

            Formatter ft;
            ft.Add<const char*>(_pendingFailure->c_str());
            ContextShowError(STR_3D_VIEW_FAILED, STR_STRING, ft);
            _pendingFailure.reset();
        }
    } // namespace

    bool IsActive()
    {
        return _active;
    }

    bool IsCapturingWindow(const WindowBase* w)
    {
        return _active && w != nullptr && w == WindowGetMain();
    }

    Camera3D& GetCamera()
    {
        return _camera;
    }

    void ApplyConfig()
    {
        const auto& general = Config::Get().general;
        auto& settings = _camera.GetSettings();
        settings.fieldOfViewDegrees = static_cast<float>(general.view3dFieldOfView);
        settings.moveSpeed = static_cast<float>(general.view3dMoveSpeed);
        settings.mouseSensitivity = static_cast<float>(general.view3dMouseSensitivity) * 0.0005f;
        settings.drawDistance = static_cast<float>(general.view3dDrawDistance * kCoordsXYStep);
        settings.invertY = general.view3dInvertY;
        settings.smoothSprites = general.view3dSmoothSprites;
        settings.shadows = general.view3dShadows;
    }

    void SetActive(bool active)
    {
        if (active == _active)
            return;

        if (active)
        {
            if (!IsSceneSupported())
            {
                ContextShowError(STR_3D_VIEW, STR_3D_VIEW_NOT_AVAILABLE_HERE, {});
                return;
            }

            auto* drawingEngine = GetContext()->GetDrawingEngine();
            if (drawingEngine == nullptr || !drawingEngine->SupportsRender3D())
            {
                ContextShowError(STR_3D_VIEW, STR_3D_VIEW_REQUIRES_OPENGL, {});
                return;
            }

            ApplyConfig();
            _camera.SetWalkCollision(TryWalk);
            PlaceCameraFromMainViewport();
            _hasLastUpdate = false;
            _active = true;
        }
        else
        {
            StopRiding();
            _active = false;
            SyncMainViewportFromCamera();
        }

        Drawing::GfxInvalidateScreen();
    }

    void Toggle()
    {
        SetActive(!_active);
    }

    void OnMouseLook(int32_t deltaX, int32_t deltaY)
    {
        _camera.Rotate(static_cast<float>(deltaX), static_cast<float>(deltaY));
    }

    void OnMouseWheel(int32_t wheel)
    {
        // Scroll up speeds the camera up, scroll down slows it down.
        auto& settings = _camera.GetSettings();
        const float factor = wheel < 0 ? 1.25f : 0.8f;
        settings.moveSpeed = std::clamp(settings.moveSpeed * factor, 16.0f, 4096.0f);
    }

    bool IsRiding()
    {
        return _ridingVehicle.has_value();
    }

    void StopRiding()
    {
        if (!_ridingVehicle.has_value())
            return;
        _ridingVehicle.reset();
        _camera.Detach();
        _camera.ApplyConstraints(GetGroundHeight);
    }

    void UpdateRideCamera()
    {
        if (!_ridingVehicle.has_value())
            return;

        // The train may have been removed (ride demolished, park loaded) since the last frame.
        auto* vehicle = getGameState().entities.tryGetEntity<Vehicle>(*_ridingVehicle);
        const auto* ride = vehicle != nullptr ? GetRide(vehicle->ride) : nullptr;
        if (vehicle == nullptr || ride == nullptr)
        {
            StopRiding();
            return;
        }

        const auto style = GetTrackRideStyle(*ride);
        TrackFrame frame;
        if (style.style == TrackMeshStyle::none || !GetVehicleFrame(*vehicle, style, frame, _rideFrames))
        {
            // Between pieces without path data; keep the last view.
            return;
        }
        const auto view = GetRiderView(frame, style);
        _camera.Attach(view.position, view.forward, view.up);
    }

    void ToggleRideCamera()
    {
        if (!_active)
            return;
        if (_ridingVehicle.has_value())
        {
            StopRiding();
            return;
        }

        // Board the closest car of a ride whose track is shown in 3D.
        const Vec3 cameraPos = _camera.GetPosition();
        std::optional<EntityId> best;
        float bestDistance = kRidePickDistance;
        for (auto* vehicle : EntityList<Vehicle>())
        {
            const auto* ride = GetRide(vehicle->ride);
            if (ride == nullptr || GetTrackRideStyle(*ride).style == TrackMeshStyle::none)
                continue;
            const auto loc = vehicle->getLocation();
            const Vec3 pos = GameToWorld(static_cast<float>(loc.x), static_cast<float>(loc.y), static_cast<float>(loc.z));
            const float distance = Length(pos - cameraPos);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = vehicle->id;
            }
        }

        if (!best.has_value())
        {
            ContextShowError(STR_3D_VIEW, STR_3D_VIEW_NO_TRAIN_NEARBY, {});
            return;
        }
        _ridingVehicle = best;
        UpdateRideCamera();
    }

    void ToggleCameraMode()
    {
        if (!_active)
            return;
        // Walking and flying only apply to the free camera.
        StopRiding();
        _camera.SetMode(_camera.GetMode() == CameraMode::freeFly ? CameraMode::firstPerson : CameraMode::freeFly);
        _camera.ApplyConstraints(GetGroundHeight);
    }

    void Update(const CameraInput& input)
    {
        ShowPendingFailure();

        if (!_active)
            return;

        if (!IsSceneSupported())
        {
            // Returned to the title screen or similar, drop back to 2D.
            StopRiding();
            _active = false;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        float deltaTime = 0.0f;
        if (_hasLastUpdate)
            deltaTime = std::chrono::duration<float>(now - _lastUpdate).count();
        _lastUpdate = now;
        _hasLastUpdate = true;

        _camera.SetBounds(GetMapBounds());
        _camera.Update(input, deltaTime, GetGroundHeight);
    }

    float GetGroundHeight(float worldX, float worldY)
    {
        const CoordsXY loc{ static_cast<int32_t>(worldX), static_cast<int32_t>(worldY) };
        if (!MapIsLocationValid(loc))
            return 0.0f;

        const int32_t z = std::max<int32_t>(TileElementHeight(loc), TileElementWaterHeight(loc));
        return GameToWorld(0, 0, static_cast<float>(z)).z;
    }

    void RequestScreenshot(const std::string& path)
    {
        _pendingScreenshot = path;
    }

    std::optional<std::string> TakeScreenshotRequest()
    {
        auto result = std::move(_pendingScreenshot);
        _pendingScreenshot.reset();
        return result;
    }

    std::string DescribeTileSprites(int32_t tileX, int32_t tileY, uint8_t rotation)
    {
        const TileCoordsXY tile{ tileX, tileY };
        if (!MapIsLocationValid(tile.toCoordsXY()))
            return "Invalid tile.";

        PaintCapture capture(rotation);
        capture.AddTile(tile);
        capture.AddEntities(tile);
        std::vector<CapturedSprite> sprites;
        capture.Collect(sprites);

        std::string result;
        char line[256];
        for (const auto& s : sprites)
        {
            const auto* g1 = GfxGetG1Element(s.image);
            std::snprintf(
                line, sizeof(line),
                "image %u item %d element %d bounds (%d %d %d)-(%d %d %d) screen %d %d size %dx%d offset %d %d%s\n",
                s.image.GetIndex(), static_cast<int>(s.interaction),
                s.element != nullptr ? static_cast<int>(s.element->getType()) : -1, s.boundsMin.x, s.boundsMin.y,
                s.boundsMin.z, s.boundsMax.x, s.boundsMax.y, s.boundsMax.z, s.screenPos.x, s.screenPos.y,
                g1 != nullptr ? g1->width : 0, g1 != nullptr ? g1->height : 0, g1 != nullptr ? g1->xOffset : 0,
                g1 != nullptr ? g1->yOffset : 0, s.image.IsBlended() ? " blended" : "");
            result += line;
        }
        if (result.empty())
            result = "No sprites.";
        return result;
    }

    void RequestStats()
    {
        _statsRequested = true;
    }

    bool TakeStatsRequest()
    {
        const bool result = _statsRequested;
        _statsRequested = false;
        return result;
    }

    void ReportFailure(const std::string& reason)
    {
        StopRiding();
        _active = false;
        _pendingFailure = reason;
        Drawing::GfxInvalidateScreen();
    }
} // namespace OpenRCT2::Render3D
