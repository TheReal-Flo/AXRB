add_executable(axrb-host-bridge
  main.cpp
  openxr_host.cpp
  pose_stream.cpp
  compositor_submit.cpp
  monado_backend.cpp
  steamvr_backend.cpp
)

target_include_directories(axrb-host-bridge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/protocol
    ${PROJECT_SOURCE_DIR}/third_party/openxr-loader-aar/extracted/prefab/modules/headers/include
)

target_link_libraries(axrb-host-bridge
  PRIVATE
    axrb_protocol
)

if(UNIX AND NOT APPLE)
  target_link_libraries(axrb-host-bridge PRIVATE dl)
endif()

if(WIN32)
  target_link_libraries(axrb-host-bridge PRIVATE d3d11 dxgi)
endif()
