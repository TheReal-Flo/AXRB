#include "runtime_internal.h"

namespace axrb::runtime::detail {

XrResult XRAPI_CALL xrCreateActionSet_impl(
    XrInstance instance,
    const XrActionSetCreateInfo* createInfo,
    XrActionSet* actionSet)
{
    log_call("xrCreateActionSet");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_SET_CREATE_INFO || actionSet == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *actionSet = fake_action_set();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyActionSet_impl(XrActionSet actionSet)
{
    log_call("xrDestroyActionSet");
    return is_valid_action_set(actionSet) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL xrCreateAction_impl(
    XrActionSet actionSet,
    const XrActionCreateInfo* createInfo,
    XrAction* action)
{
    log_call("xrCreateAction");
    if (!is_valid_action_set(actionSet)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_CREATE_INFO || action == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    g_actionHandles.push_back({0xAABBCCDD00000100ULL + g_actionCount});
    g_actionHandles.back().type = createInfo->actionType;
    *action = fake_action(g_actionCount++);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyAction_impl(XrAction action)
{
    log_call("xrDestroyAction");
    if (!is_valid_action(action)) return XR_ERROR_HANDLE_INVALID;
    reinterpret_cast<RuntimeHandle*>(action)->magic = 0;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrStringToPath_impl(XrInstance instance, const char* pathString, XrPath* path)
{
    log_call("xrStringToPath");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (pathString == nullptr || path == nullptr || pathString[0] != '/') {
        return XR_ERROR_PATH_FORMAT_INVALID;
    }
    const std::string_view requested{pathString};
    if (requested.size() >= 256) return XR_ERROR_PATH_FORMAT_INVALID;
    for (uint32_t i = 0; i < g_pathCount; ++i) {
        if (requested == g_paths[i].text) {
            *path = g_paths[i].path;
            return XR_SUCCESS;
        }
    }
    g_paths.emplace_back();
    PathRecord& record = g_paths[g_pathCount++];
    record.path = g_nextPath++;
    record.text = requested;
    *path = record.path;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrPathToString_impl(
    XrInstance instance,
    XrPath path,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
    log_call("xrPathToString");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const char* text = path_text(path);
    if (text[0] == '\0') {
        return XR_ERROR_PATH_INVALID;
    }
    const uint32_t pathSize = static_cast<uint32_t>(std::strlen(text) + 1);
    if (!bufferCountOutput) return XR_ERROR_VALIDATION_FAILURE;
    *bufferCountOutput = pathSize;
    if (!bufferCapacityInput) return XR_SUCCESS;
    if (bufferCapacityInput < pathSize) return XR_ERROR_SIZE_INSUFFICIENT;
    if (!buffer) return XR_ERROR_VALIDATION_FAILURE;
    std::memcpy(buffer, text, pathSize);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrSuggestInteractionProfileBindings_impl(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
    log_call("xrSuggestInteractionProfileBindings");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (suggestedBindings == nullptr ||
        suggestedBindings->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    // Present a Touch-compatible logical controller to Android. SteamVR maps
    // the user's physical controller to these semantic inputs on Windows.
    if (std::string_view(path_text(suggestedBindings->interactionProfile)) == "/interaction_profiles/oculus/touch_controller") {
        if (suggestedBindings->countSuggestedBindings && !suggestedBindings->suggestedBindings) return XR_ERROR_VALIDATION_FAILURE;
        for (uint32_t i = 0; i < suggestedBindings->countSuggestedBindings; ++i) {
            const auto& binding = suggestedBindings->suggestedBindings[i];
            if (!is_valid_action(binding.action)) return XR_ERROR_HANDLE_INVALID;
#if defined(__ANDROID__)
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Input", "binding action=%p type=%d path=%s",
                reinterpret_cast<void*>(binding.action), reinterpret_cast<ActionRecord*>(binding.action)->type, path_text(binding.binding));
#endif
            auto& paths = reinterpret_cast<ActionRecord*>(binding.action)->bindings;
            if (std::find(paths.begin(), paths.end(), binding.binding) == paths.end()) paths.push_back(binding.binding);
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrAttachSessionActionSets_impl(
    XrSession session,
    const XrSessionActionSetsAttachInfo* attachInfo)
{
    log_call("xrAttachSessionActionSets");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (attachInfo == nullptr || attachInfo->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    g_pendingInteractionProfileEvent = true;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrSyncActions_impl(XrSession session, const XrActionsSyncInfo* syncInfo)
{
    log_call("xrSyncActions");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (syncInfo == nullptr || syncInfo->type != XR_TYPE_ACTIONS_SYNC_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const auto& frame = pose_client().latest_pose_frame();
    const XrTime now = monotonic_time_ns();
    for (auto& action : g_actionHandles) {
        if (!action.magic) continue;
        std::array<axrb::protocol::InputValue, 3> values{};
        for (XrPath path : action.bindings) {
            std::string_view text{path_text(path)};
            for (size_t hand = 0; hand < 2; ++hand) {
                const std::string_view prefix = hand == 0 ? "/user/hand/left/input/" : "/user/hand/right/input/";
                if (!text.starts_with(prefix)) continue;
                auto value = axrb::protocol::controller_binding(frame.controllers[hand], text.substr(prefix.size()));
                if (text.substr(prefix.size()) == "aim/pose" && frame.version >= 3) value.active = frame.aim_active[hand] != 0;
                if (action.type == XR_ACTION_TYPE_BOOLEAN_INPUT) value.x = value.x > 0.5f ? 1.0f : 0.0f;
                for (size_t slot : {hand, size_t{2}}) {
                    auto& combined = values[slot];
                    const bool active = combined.active || value.active;
                    if (value.x * value.x + value.y * value.y > combined.x * combined.x + combined.y * combined.y) combined = value;
                    combined.active = active;
                }
            }
        }
        for (size_t i = 0; i < values.size(); ++i) {
            auto& sample = action.samples[i];
            const auto& value = values[i];
            sample.changed = value.active && (sample.value.active != value.active || sample.value.x != value.x || sample.value.y != value.y);
            if (sample.changed) sample.changedAt = now;
            sample.value = value;
        }
    }
    return XR_SUCCESS;
}

const ActionSample& action_sample(const XrActionStateGetInfo& info) {
    const auto kind = action_space_kind(info.subactionPath);
    return reinterpret_cast<const ActionRecord*>(info.action)->samples[
        kind == SpaceKind::LeftHand ? 0 : kind == SpaceKind::RightHand ? 1 : 2];
}

XrResult XRAPI_CALL xrGetActionStateBoolean_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateBoolean* state)
{
    log_call("xrGetActionStateBoolean");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_BOOLEAN || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (reinterpret_cast<ActionRecord*>(getInfo->action)->type != XR_ACTION_TYPE_BOOLEAN_INPUT) return XR_ERROR_ACTION_TYPE_MISMATCH;
    const auto& sample = action_sample(*getInfo);
    state->currentState = sample.value.x > 0.5f;
    state->changedSinceLastSync = sample.changed;
    state->lastChangeTime = sample.changedAt;
    state->isActive = sample.value.active;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStateFloat_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateFloat* state)
{
    log_call("xrGetActionStateFloat");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_FLOAT || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (reinterpret_cast<ActionRecord*>(getInfo->action)->type != XR_ACTION_TYPE_FLOAT_INPUT) return XR_ERROR_ACTION_TYPE_MISMATCH;
    const auto& sample = action_sample(*getInfo);
    state->currentState = sample.value.x;
    state->changedSinceLastSync = sample.changed;
    state->lastChangeTime = sample.changedAt;
    state->isActive = sample.value.active;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStateVector2f_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateVector2f* state)
{
    log_call("xrGetActionStateVector2f");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_VECTOR2F || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (reinterpret_cast<ActionRecord*>(getInfo->action)->type != XR_ACTION_TYPE_VECTOR2F_INPUT) return XR_ERROR_ACTION_TYPE_MISMATCH;
    const auto& sample = action_sample(*getInfo);
    state->currentState = {sample.value.x, sample.value.y};
    state->changedSinceLastSync = sample.changed;
    state->lastChangeTime = sample.changedAt;
    state->isActive = sample.value.active;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStatePose_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStatePose* state)
{
    log_call("xrGetActionStatePose");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_POSE || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (reinterpret_cast<ActionRecord*>(getInfo->action)->type != XR_ACTION_TYPE_POSE_INPUT) return XR_ERROR_ACTION_TYPE_MISMATCH;
    state->isActive = action_sample(*getInfo).value.active;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetCurrentInteractionProfile_impl(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState* interactionProfile)
{
    log_call("xrGetCurrentInteractionProfile");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (topLevelUserPath == XR_NULL_PATH || interactionProfile == nullptr ||
        interactionProfile->type != XR_TYPE_INTERACTION_PROFILE_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const auto kind = action_space_kind(topLevelUserPath);
    if (kind != SpaceKind::LeftHand && kind != SpaceKind::RightHand) return XR_ERROR_PATH_UNSUPPORTED;
    return xrStringToPath_impl(fake_instance(), "/interaction_profiles/oculus/touch_controller", &interactionProfile->interactionProfile);
}

XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction_impl(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t* sourceCountOutput,
    XrPath* sources)
{
    log_call("xrEnumerateBoundSourcesForAction");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (enumerateInfo == nullptr || sourceCountOutput == nullptr ||
        enumerateInfo->type != XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO || !is_valid_action(enumerateInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const auto& bindings = reinterpret_cast<ActionRecord*>(enumerateInfo->action)->bindings;
    *sourceCountOutput = static_cast<uint32_t>(bindings.size());
    if (!sourceCapacityInput) return XR_SUCCESS;
    if (sourceCapacityInput < bindings.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    if (!sources) return XR_ERROR_VALIDATION_FAILURE;
    std::copy(bindings.begin(), bindings.end(), sources);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetInputSourceLocalizedName_impl(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo* getInfo,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
    log_call("xrGetInputSourceLocalizedName");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO || bufferCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    constexpr const char kName[] = "AXRB Input";
    constexpr uint32_t kNameSize = sizeof(kName);
    *bufferCountOutput = kNameSize;
    if (buffer != nullptr) {
        if (bufferCapacityInput < kNameSize) {
            return XR_ERROR_VALIDATION_FAILURE;
        }
        std::memcpy(buffer, kName, kNameSize);
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrApplyHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    const XrHapticBaseHeader* hapticFeedback)
{
    log_call("xrApplyHapticFeedback");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (hapticActionInfo == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO ||
        hapticFeedback == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrStopHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo)
{
    log_call("xrStopHapticFeedback");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (hapticActionInfo == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
