#pragma once
// Standard OpenXR extension ABI declarations (Khronos, Apache-2.0 OR MIT).
#include "openxr_minimal.h"
using XrHandTrackerEXT = struct XrHandTrackerEXT_T*;
using XrSpaceVelocityFlags = uint64_t;
struct XrVector3f { float x, y, z; };
constexpr uint32_t XR_HAND_JOINT_COUNT_EXT = 26;
constexpr XrStructureType XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT = static_cast<XrStructureType>(1000051000);
constexpr XrStructureType XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT = static_cast<XrStructureType>(1000051001);
constexpr XrStructureType XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT = static_cast<XrStructureType>(1000051002);
constexpr XrStructureType XR_TYPE_HAND_JOINT_LOCATIONS_EXT = static_cast<XrStructureType>(1000051003);
constexpr XrStructureType XR_TYPE_HAND_JOINT_VELOCITIES_EXT = static_cast<XrStructureType>(1000051004);
constexpr XrStructureType XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT = static_cast<XrStructureType>(1000428000);
constexpr XrStructureType XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT = static_cast<XrStructureType>(1000428001);
typedef enum XrHandEXT {
    XR_HAND_LEFT_EXT = 1,
    XR_HAND_RIGHT_EXT = 2,
    XR_HAND_MAX_ENUM_EXT = 0x7FFFFFFF
} XrHandEXT;

typedef enum XrHandJointEXT {
    XR_HAND_JOINT_PALM_EXT = 0,
    XR_HAND_JOINT_WRIST_EXT = 1,
    XR_HAND_JOINT_THUMB_METACARPAL_EXT = 2,
    XR_HAND_JOINT_THUMB_PROXIMAL_EXT = 3,
    XR_HAND_JOINT_THUMB_DISTAL_EXT = 4,
    XR_HAND_JOINT_THUMB_TIP_EXT = 5,
    XR_HAND_JOINT_INDEX_METACARPAL_EXT = 6,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT = 7,
    XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT = 8,
    XR_HAND_JOINT_INDEX_DISTAL_EXT = 9,
    XR_HAND_JOINT_INDEX_TIP_EXT = 10,
    XR_HAND_JOINT_MIDDLE_METACARPAL_EXT = 11,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT = 12,
    XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT = 13,
    XR_HAND_JOINT_MIDDLE_DISTAL_EXT = 14,
    XR_HAND_JOINT_MIDDLE_TIP_EXT = 15,
    XR_HAND_JOINT_RING_METACARPAL_EXT = 16,
    XR_HAND_JOINT_RING_PROXIMAL_EXT = 17,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT = 18,
    XR_HAND_JOINT_RING_DISTAL_EXT = 19,
    XR_HAND_JOINT_RING_TIP_EXT = 20,
    XR_HAND_JOINT_LITTLE_METACARPAL_EXT = 21,
    XR_HAND_JOINT_LITTLE_PROXIMAL_EXT = 22,
    XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT = 23,
    XR_HAND_JOINT_LITTLE_DISTAL_EXT = 24,
    XR_HAND_JOINT_LITTLE_TIP_EXT = 25,
    XR_HAND_JOINT_MAX_ENUM_EXT = 0x7FFFFFFF
} XrHandJointEXT;

typedef enum XrHandJointSetEXT {
    XR_HAND_JOINT_SET_DEFAULT_EXT = 0,
    XR_HAND_JOINT_SET_HAND_WITH_FOREARM_ULTRALEAP = 1000149000,
    XR_HAND_JOINT_SET_MAX_ENUM_EXT = 0x7FFFFFFF
} XrHandJointSetEXT;
// XrSystemHandTrackingPropertiesEXT extends XrSystemProperties
typedef struct XrSystemHandTrackingPropertiesEXT {
    XrStructureType       type;
    void*    next;
    XrBool32              supportsHandTracking;
} XrSystemHandTrackingPropertiesEXT;

typedef struct XrHandTrackerCreateInfoEXT {
    XrStructureType             type;
    const void*    next;
    XrHandEXT                   hand;
    XrHandJointSetEXT           handJointSet;
} XrHandTrackerCreateInfoEXT;

typedef struct XrHandJointsLocateInfoEXT {
    XrStructureType             type;
    const void*    next;
    XrSpace                     baseSpace;
    XrTime                      time;
} XrHandJointsLocateInfoEXT;

typedef struct XrHandJointLocationEXT {
    XrSpaceLocationFlags    locationFlags;
    XrPosef                 pose;
    float                   radius;
} XrHandJointLocationEXT;

typedef struct XrHandJointVelocityEXT {
    XrSpaceVelocityFlags    velocityFlags;
    XrVector3f              linearVelocity;
    XrVector3f              angularVelocity;
} XrHandJointVelocityEXT;

typedef struct XrHandJointLocationsEXT {
    XrStructureType            type;
    void*         next;
    XrBool32                   isActive;
    uint32_t                   jointCount;
    XrHandJointLocationEXT*    jointLocations;
} XrHandJointLocationsEXT;

// XrHandJointVelocitiesEXT extends XrHandJointLocationsEXT
typedef struct XrHandJointVelocitiesEXT {
    XrStructureType            type;
    void*         next;
    uint32_t                   jointCount;
    XrHandJointVelocityEXT*    jointVelocities;
} XrHandJointVelocitiesEXT;

typedef enum XrHandTrackingDataSourceEXT {
    XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT = 1,
    XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT = 2,
    XR_HAND_TRACKING_DATA_SOURCE_MAX_ENUM_EXT = 0x7FFFFFFF
} XrHandTrackingDataSourceEXT;
// XrHandTrackingDataSourceInfoEXT extends XrHandTrackerCreateInfoEXT
typedef struct XrHandTrackingDataSourceInfoEXT {
    XrStructureType                 type;
    const void*        next;
    uint32_t                        requestedDataSourceCount;
    XrHandTrackingDataSourceEXT*    requestedDataSources;
} XrHandTrackingDataSourceInfoEXT;

// XrHandTrackingDataSourceStateEXT extends XrHandJointLocationsEXT
typedef struct XrHandTrackingDataSourceStateEXT {
    XrStructureType                type;
    void*             next;
    XrBool32                       isActive;
    XrHandTrackingDataSourceEXT    dataSource;
} XrHandTrackingDataSourceStateEXT;



