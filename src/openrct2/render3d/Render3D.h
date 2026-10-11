/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "Camera3D.h"

#include <optional>
#include <string>

namespace OpenRCT2
{
    struct WindowBase;
}

namespace OpenRCT2::Render3D
{
    // Whether the main viewport is currently shown in 3D.
    bool IsActive();

    // True when the window is the main window and the 3D view owns its viewport (mouse look, no 2D interaction).
    bool IsCapturingWindow(const WindowBase* w);

    // Switches between the isometric view and the 3D view. Shows an error if the 3D view is not available.
    void Toggle();
    void SetActive(bool active);

    Camera3D& GetCamera();

    // Copies the 3D view options from the configuration into the camera.
    void ApplyConfig();

    // Camera control, called by the UI input layer.
    void OnMouseLook(int32_t deltaX, int32_t deltaY);
    void OnMouseWheel(int32_t wheel);
    void ToggleCameraMode();

    // Rides along in the front seat of the nearest train (or stops riding). Shows a message if no train is near.
    void ToggleRideCamera();
    bool IsRiding();
    void StopRiding();
    // Moves the camera with the ridden vehicle; called by the renderer each frame, while entity positions are
    // interpolated for drawing.
    void UpdateRideCamera();
    void Update(const CameraInput& input);

    // Height of the highest walkable surface (terrain or water) at a world position, in world units.
    float GetGroundHeight(float worldX, float worldY);

    // Called by the renderer when it cannot draw the 3D view. Turns the 3D view off and reports the reason.
    void ReportFailure(const std::string& reason);

    // Asks the renderer to save the next composited frame (3D view and UI) as a PNG file.
    void RequestScreenshot(const std::string& path);
    // Returns and clears the pending screenshot request, if any.
    std::optional<std::string> TakeScreenshotRequest();

    // Describes the sprites the painter emits for a tile (and the entities on it) in the given view rotation,
    // one per line, for diagnosing how objects are reconstructed.
    std::string DescribeTileSprites(int32_t tileX, int32_t tileY, uint8_t rotation);

    // Asks the renderer to log statistics about the next frame (chunks, vertices, build times).
    void RequestStats();
    // Returns and clears the pending statistics request.
    bool TakeStatsRequest();
} // namespace OpenRCT2::Render3D
