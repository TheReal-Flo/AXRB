add_library(openxr_runtime SHARED
  openxr_dispatch/runtime_entry.cpp
  session.cpp
  pose_client.cpp
  swapchain.cpp
  input.cpp
  vulkan_backend.cpp
)

target_include_directories(openxr_runtime
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/protocol
)

target_link_libraries(openxr_runtime
  PRIVATE
    axrb_protocol
)

if(ANDROID)
  target_link_libraries(openxr_runtime PRIVATE log GLESv3 vulkan)
  option(AXRB_BUILD_VULKAN_SMOKE "Build the on-device Vulkan readback regression" OFF)
  if(AXRB_BUILD_VULKAN_SMOKE)
    add_executable(axrb_vulkan_smoke ../tests/vulkan_android_smoke.cpp vulkan_backend.cpp)
    target_include_directories(axrb_vulkan_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/protocol)
    target_link_libraries(axrb_vulkan_smoke PRIVATE vulkan log)
  endif()
endif()

set_target_properties(openxr_runtime PROPERTIES
  OUTPUT_NAME openxr_runtime
)
