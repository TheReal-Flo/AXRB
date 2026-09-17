#pragma once
#include "pose_frame.h"
#include <string_view>

namespace axrb::protocol {
struct InputValue { bool active = false; float x = 0, y = 0; };

inline InputValue controller_binding(const ControllerInput& input, std::string_view component) {
    if (!input.active) return {};
    const auto button = [&](uint32_t bit) { return InputValue{true, (input.buttons & bit) ? 1.0f : 0.0f, 0}; };
    if (component == "trigger/value") return {true, input.trigger};
    if (component == "trigger/click") return {true, input.trigger > 0.5f ? 1.0f : 0.0f};
    if (component == "squeeze/value") return {true, input.squeeze};
    if (component == "squeeze/click") return {true, input.squeeze > 0.5f ? 1.0f : 0.0f};
    if (component == "thumbstick" || component == "trackpad") return {true, input.stick_x, input.stick_y};
    if (component == "thumbstick/x" || component == "trackpad/x") return {true, input.stick_x};
    if (component == "thumbstick/y" || component == "trackpad/y") return {true, input.stick_y};
    if (component == "a/click" || component == "x/click" || component == "select/click") return button(PrimaryClick);
    if (component == "b/click" || component == "y/click") return button(SecondaryClick);
    if (component == "menu/click") return button(MenuClick);
    if (component == "thumbstick/click" || component == "trackpad/click") return button(StickClick);
    if (component == "a/touch" || component == "x/touch") return button(PrimaryTouch);
    if (component == "b/touch" || component == "y/touch") return button(SecondaryTouch);
    if (component == "trigger/touch") return button(TriggerTouch);
    if (component == "thumbstick/touch" || component == "trackpad/touch") return button(StickTouch);
    if (component == "thumbrest/touch") return button(ThumbrestTouch);
    if (component == "grip/pose" || component == "aim/pose") return {true};
    return {};
}
}
