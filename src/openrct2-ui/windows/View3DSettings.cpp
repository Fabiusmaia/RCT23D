/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <openrct2-ui/UiStringIds.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/localisation/Formatter.h>
#include <openrct2/localisation/StringIds.h>
#include <openrct2/render3d/Render3D.h>
#include <openrct2/ui/WindowManager.h>

namespace OpenRCT2::Ui::Windows
{
    enum WindowView3DSettingsWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_ENABLE,
        WIDX_FOV,
        WIDX_FOV_INCREASE,
        WIDX_FOV_DECREASE,
        WIDX_SPEED,
        WIDX_SPEED_INCREASE,
        WIDX_SPEED_DECREASE,
        WIDX_SENSITIVITY,
        WIDX_SENSITIVITY_INCREASE,
        WIDX_SENSITIVITY_DECREASE,
        WIDX_DRAW_DISTANCE,
        WIDX_DRAW_DISTANCE_INCREASE,
        WIDX_DRAW_DISTANCE_DECREASE,
        WIDX_INVERT_Y,
        WIDX_WALKING,
        WIDX_SMOOTH,
        WIDX_SHADOWS,
        WIDX_RIDE,
    };

    static constexpr ScreenSize kWindowSize = { 250, 240 };
    static constexpr int32_t kSpinnerX = 160;
    static constexpr ScreenSize kSpinnerSize = { 80, 12 };
    static constexpr int32_t kHelpTop = 180;

    // clang-format off
    static constexpr auto _view3DSettingsWidgets = makeWidgets(
        makeWindowShim(STR_3D_VIEW_SETTINGS, kWindowSize),
        makeWidget                ({ 10,  18}, {230, 12}, WidgetType::checkbox, WindowColour::primary, STR_3D_VIEW_ENABLE, STR_SHORTCUT_TOGGLE_3D_VIEW),
        makeHoldableSpinnerWidgets({ kSpinnerX,  36}, kSpinnerSize, WidgetType::spinner, WindowColour::primary),
        makeHoldableSpinnerWidgets({ kSpinnerX,  52}, kSpinnerSize, WidgetType::spinner, WindowColour::primary),
        makeHoldableSpinnerWidgets({ kSpinnerX,  68}, kSpinnerSize, WidgetType::spinner, WindowColour::primary),
        makeHoldableSpinnerWidgets({ kSpinnerX,  84}, kSpinnerSize, WidgetType::spinner, WindowColour::primary),
        makeWidget                ({ 10, 102}, {230, 12}, WidgetType::checkbox, WindowColour::primary, STR_3D_VIEW_INVERT_Y),
        makeWidget                ({ 10, 116}, {230, 12}, WidgetType::checkbox, WindowColour::primary, STR_3D_VIEW_WALKING_CAMERA, STR_3D_VIEW_WALKING_CAMERA_TIP),
        makeWidget                ({ 10, 130}, {230, 12}, WidgetType::checkbox, WindowColour::primary, STR_3D_VIEW_SMOOTH_SPRITES, STR_3D_VIEW_SMOOTH_SPRITES_TIP),
        makeWidget                ({ 10, 144}, {230, 12}, WidgetType::checkbox, WindowColour::primary, STR_3D_VIEW_SHADOWS),
        makeWidget                ({ 10, 160}, {230, 14}, WidgetType::button,   WindowColour::primary, STR_3D_VIEW_RIDE_NEAREST, STR_SHORTCUT_3D_RIDE_NEAREST)
    );
    // clang-format on

    class View3DSettingsWindow final : public Window
    {
    public:
        void onOpen() override
        {
            setWidgets(_view3DSettingsWidgets);
            WindowInitScrollWidgets(*this);
        }

        void onMouseUp(WidgetIndex widgetIndex) override
        {
            auto& general = Config::Get().general;
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    break;
                case WIDX_ENABLE:
                    Render3D::Toggle();
                    break;
                case WIDX_RIDE:
                    if (!Render3D::IsActive())
                        Render3D::SetActive(true);
                    Render3D::ToggleRideCamera();
                    break;
                case WIDX_INVERT_Y:
                    general.view3dInvertY = !general.view3dInvertY;
                    SaveAndApply();
                    break;
                case WIDX_SMOOTH:
                    general.view3dSmoothSprites = !general.view3dSmoothSprites;
                    SaveAndApply();
                    break;
                case WIDX_SHADOWS:
                    general.view3dShadows = !general.view3dShadows;
                    SaveAndApply();
                    break;
                case WIDX_WALKING:
                {
                    auto& camera = Render3D::GetCamera();
                    if (Render3D::IsActive())
                        Render3D::ToggleCameraMode();
                    else
                        camera.SetMode(
                            camera.GetMode() == Render3D::CameraMode::freeFly ? Render3D::CameraMode::firstPerson
                                                                             : Render3D::CameraMode::freeFly);
                    break;
                }
            }
            invalidate();
        }

        void onMouseDown(WidgetIndex widgetIndex) override
        {
            auto& general = Config::Get().general;
            switch (widgetIndex)
            {
                case WIDX_FOV_INCREASE:
                    general.view3dFieldOfView = std::min(general.view3dFieldOfView + 5, 110);
                    break;
                case WIDX_FOV_DECREASE:
                    general.view3dFieldOfView = std::max(general.view3dFieldOfView - 5, 40);
                    break;
                case WIDX_SPEED_INCREASE:
                    general.view3dMoveSpeed = std::min(general.view3dMoveSpeed + std::max(general.view3dMoveSpeed / 4, 8), 4096);
                    break;
                case WIDX_SPEED_DECREASE:
                    general.view3dMoveSpeed = std::max(general.view3dMoveSpeed - std::max(general.view3dMoveSpeed / 5, 8), 16);
                    break;
                case WIDX_SENSITIVITY_INCREASE:
                    general.view3dMouseSensitivity = std::min(general.view3dMouseSensitivity + 1, 20);
                    break;
                case WIDX_SENSITIVITY_DECREASE:
                    general.view3dMouseSensitivity = std::max(general.view3dMouseSensitivity - 1, 1);
                    break;
                case WIDX_DRAW_DISTANCE_INCREASE:
                    general.view3dDrawDistance = std::min(general.view3dDrawDistance + 25, 200);
                    break;
                case WIDX_DRAW_DISTANCE_DECREASE:
                    general.view3dDrawDistance = std::max(general.view3dDrawDistance - 25, 25);
                    break;
                default:
                    return;
            }
            SaveAndApply();
            invalidate();
        }

        void onUpdate() override
        {
            // The view can also be switched with the keyboard shortcut while this window is open.
            invalidate();
        }

        void onPrepareDraw() override
        {
            const auto& general = Config::Get().general;
            setCheckboxValue(WIDX_ENABLE, Render3D::IsActive());
            setCheckboxValue(WIDX_INVERT_Y, general.view3dInvertY);
            setCheckboxValue(WIDX_SMOOTH, general.view3dSmoothSprites);
            setCheckboxValue(WIDX_SHADOWS, general.view3dShadows);
            setCheckboxValue(WIDX_WALKING, Render3D::GetCamera().GetMode() == Render3D::CameraMode::firstPerson);
            setWidgetPressed(WIDX_RIDE, Render3D::IsRiding());
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            WindowDrawWidgets(*this, rt);

            const auto& general = Config::Get().general;
            drawRow(rt, WIDX_FOV, STR_3D_VIEW_FIELD_OF_VIEW, general.view3dFieldOfView);
            drawRow(rt, WIDX_SPEED, STR_3D_VIEW_MOVE_SPEED, general.view3dMoveSpeed);
            drawRow(rt, WIDX_SENSITIVITY, STR_3D_VIEW_MOUSE_SENSITIVITY, general.view3dMouseSensitivity);
            drawRow(rt, WIDX_DRAW_DISTANCE, STR_3D_VIEW_DRAW_DISTANCE, general.view3dDrawDistance);

            drawTextWrapped(
                rt, windowPos + ScreenCoordsXY{ 10, kHelpTop }, kWindowSize.width - 20, STR_3D_VIEW_CONTROLS_HELP,
                { colours[0] });
        }

    private:
        void drawRow(Drawing::RenderTarget& rt, WidgetIndex spinner, StringId label, int32_t value)
        {
            const auto& widget = widgets[spinner];
            drawText(rt, windowPos + ScreenCoordsXY{ 10, widget.top + 1 }, label, { colours[0] });

            auto ft = Formatter();
            ft.Add<int32_t>(value);
            drawText(rt, windowPos + ScreenCoordsXY{ widget.left + 2, widget.top + 1 }, STR_FORMAT_INTEGER, ft, { colours[0] });
        }

        static void SaveAndApply()
        {
            Config::Save();
            Render3D::ApplyConfig();
        }
    };

    WindowBase* View3DSettingsOpen()
    {
        auto* windowMgr = GetWindowManager();
        auto* window = windowMgr->BringToFrontByClass(WindowClass::view3DSettings);
        if (window == nullptr)
        {
            window = windowMgr->Create<View3DSettingsWindow>(
                WindowClass::view3DSettings, ScreenCoordsXY(40, 40), kWindowSize);
        }
        return window;
    }
} // namespace OpenRCT2::Ui::Windows
