#include "runtime_internal.h"

namespace axrb::runtime::detail {

HandTrackerRecord* find_hand_tracker(XrHandTrackerEXT tracker) {
    for (auto& record : g_handTrackers) if (record.alive && reinterpret_cast<XrHandTrackerEXT>(&record) == tracker) return &record;
    return nullptr;
}

XrResult XRAPI_CALL xrCreateHandTrackerEXT_impl(XrSession session, const XrHandTrackerCreateInfoEXT* info, XrHandTrackerEXT* tracker) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT || !tracker ||
        (info->hand != XR_HAND_LEFT_EXT && info->hand != XR_HAND_RIGHT_EXT) || info->handJointSet != XR_HAND_JOINT_SET_DEFAULT_EXT)
        return XR_ERROR_VALIDATION_FAILURE;
    uint32_t sources = 3;
    struct InputHeader { XrStructureType type; const void* next; };
    for (auto* next = static_cast<const InputHeader*>(info->next); next; next = static_cast<const InputHeader*>(next->next)) {
        if (next->type != XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT) continue;
        const auto* request = reinterpret_cast<const XrHandTrackingDataSourceInfoEXT*>(next);
        if (!request->requestedDataSourceCount || !request->requestedDataSources) return XR_ERROR_VALIDATION_FAILURE;
        sources = 0;
        for (uint32_t i = 0; i < request->requestedDataSourceCount; ++i) {
            const auto value = request->requestedDataSources[i];
            if (value != XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT && value != XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT) return XR_ERROR_VALIDATION_FAILURE;
            sources |= 1u << (static_cast<uint32_t>(value) - 1);
        }
    }
    g_handTrackers.push_back({true, info->hand == XR_HAND_LEFT_EXT ? 0u : 1u, sources});
    *tracker = reinterpret_cast<XrHandTrackerEXT>(&g_handTrackers.back());
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Hands", "created hand=%u sourceMask=%u", g_handTrackers.back().hand, sources);
#endif
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyHandTrackerEXT_impl(XrHandTrackerEXT tracker) {
    auto* record = find_hand_tracker(tracker);
    if (!record) return XR_ERROR_HANDLE_INVALID;
    record->alive = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrLocateHandJointsEXT_impl(XrHandTrackerEXT tracker, const XrHandJointsLocateInfoEXT* info, XrHandJointLocationsEXT* locations) {
    auto* record = find_hand_tracker(tracker);
#if defined(__ANDROID__)
    static unsigned requests = 0;
    if (requests++ < 12) __android_log_print(ANDROID_LOG_INFO, "AXRB.Hands", "locate request tracker=%p valid=%d count=%u base=%p", reinterpret_cast<void*>(tracker), record != nullptr, locations ? locations->jointCount : 0, info ? reinterpret_cast<void*>(info->baseSpace) : nullptr);
#endif
    if (!record) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT || !locations ||
        locations->type != XR_TYPE_HAND_JOINT_LOCATIONS_EXT || locations->jointCount != XR_HAND_JOINT_COUNT_EXT || !locations->jointLocations)
        return XR_ERROR_VALIDATION_FAILURE;
    auto* base = find_space(info->baseSpace);
    if (!base) return XR_ERROR_HANDLE_INVALID;
    const auto& frame = pose_client().latest_pose_frame();
    const auto& hand = frame.hands[record->hand];
    locations->isActive = hand.active && (hand.source == 0 || (hand.source <= 2 && (record->sources & (1u << (hand.source - 1)))));
    if (frame.version >= 5 && base->kind == SpaceKind::Local && (frame.local_origin_flags & 3) != 3)
        locations->isActive = 0;
    const auto inverseBase = inverse_pose(world_pose_for_space(*base, frame));
    for (uint32_t joint = 0; joint < XR_HAND_JOINT_COUNT_EXT; ++joint) {
        auto& out = locations->jointLocations[joint]; const auto& in = hand.joints[joint];
        out.locationFlags = locations->isActive ? in.flags : 0;
        out.pose = locations->isActive ? multiply_pose(inverseBase, protocol_pose_to_xr(in.pose)) : identity_pose();
        out.radius = locations->isActive ? in.radius : 0;
    }
    struct OutputHeader { XrStructureType type; void* next; };
    for (auto* next = static_cast<OutputHeader*>(locations->next); next; next = static_cast<OutputHeader*>(next->next)) {
        if (next->type == XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT) {
            auto* out = reinterpret_cast<XrHandTrackingDataSourceStateEXT*>(next);
            out->isActive = locations->isActive && hand.source != 0;
            out->dataSource = static_cast<XrHandTrackingDataSourceEXT>(hand.source);
        } else if (next->type == XR_TYPE_HAND_JOINT_VELOCITIES_EXT) {
            auto* out = reinterpret_cast<XrHandJointVelocitiesEXT*>(next);
            if (out->jointCount != XR_HAND_JOINT_COUNT_EXT || !out->jointVelocities) return XR_ERROR_VALIDATION_FAILURE;
            for (uint32_t joint = 0; joint < out->jointCount; ++joint) out->jointVelocities[joint] = {};
        }
    }
#if defined(__ANDROID__)
    static uint32_t reports = 0;
    if (++reports % 600 == 0) __android_log_print(ANDROID_LOG_INFO, "AXRB.Hands", "locate hand=%u active=%u source=%u", record->hand, locations->isActive, hand.source);
#endif
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
