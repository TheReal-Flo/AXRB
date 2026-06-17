add_library(openxr_runtime SHARED
  openxr_dispatch/runtime_entry.cpp
  session.cpp
  pose_client.cpp
  swapchain.cpp
  input.cpp
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
  target_link_libraries(openxr_runtime PRIVATE log GLESv3)
endif()

set_target_properties(openxr_runtime PROPERTIES
  OUTPUT_NAME openxr_runtime
)
