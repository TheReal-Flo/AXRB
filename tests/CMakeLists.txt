add_test(NAME axrb_host_bridge_starts COMMAND axrb-host-bridge)

add_executable(axrb_menu_shortcut_smoke menu_shortcut_smoke.cpp)
target_link_libraries(axrb_menu_shortcut_smoke PRIVATE axrb_protocol)
add_test(NAME axrb_menu_shortcut COMMAND axrb_menu_shortcut_smoke)

add_executable(axrb_runtime_smoke runtime_smoke.cpp)
target_include_directories(axrb_runtime_smoke
  PRIVATE
    ${PROJECT_SOURCE_DIR}/android-runtime
)
target_link_libraries(axrb_runtime_smoke PRIVATE openxr_runtime)
add_test(NAME axrb_runtime_negotiates_and_frames COMMAND axrb_runtime_smoke)
set_tests_properties(axrb_runtime_negotiates_and_frames PROPERTIES
  ENVIRONMENT "PATH=$<TARGET_FILE_DIR:openxr_runtime>;$ENV{PATH}"
)

if(NOT WIN32)
  add_executable(axrb_gpu_transport_smoke gpu_transport_smoke.cpp)
  target_link_libraries(axrb_gpu_transport_smoke PRIVATE axrb_protocol)
  add_test(NAME axrb_gpu_transport_passes_fd COMMAND axrb_gpu_transport_smoke)
endif()

add_executable(axrb_video_transport_smoke video_transport_smoke.cpp)
target_link_libraries(axrb_video_transport_smoke PRIVATE axrb_protocol)
add_test(NAME axrb_video_transport_reassembles_udp_frames COMMAND axrb_video_transport_smoke)

add_subdirectory(hello_xr)

if(WIN32)
  add_executable(axrb_windows_gpu_transport_smoke windows_gpu_transport_smoke.cpp)
  target_link_libraries(axrb_windows_gpu_transport_smoke PRIVATE axrb_protocol ws2_32)
  add_test(NAME axrb_windows_gpu_metadata_and_ack COMMAND axrb_windows_gpu_transport_smoke)
  set_tests_properties(axrb_windows_gpu_metadata_and_ack PROPERTIES TIMEOUT 10)
endif()

add_executable(axrb_pose_stream_decoder_smoke pose_stream_decoder_smoke.cpp)
target_link_libraries(axrb_pose_stream_decoder_smoke PRIVATE axrb_protocol)
add_test(NAME axrb_pose_stream_handles_fragmentation COMMAND axrb_pose_stream_decoder_smoke)

# Exercise real runtime dispatch/space transforms with deterministic host input.
if(NOT ANDROID)
  add_executable(axrb_runtime_input_smoke runtime_smoke.cpp runtime_input_fixture.cpp
    ../android-runtime/session.cpp ../android-runtime/openxr_dispatch/runtime_entry.cpp)
  target_include_directories(axrb_runtime_input_smoke PRIVATE ${PROJECT_SOURCE_DIR}/android-runtime)
  target_compile_definitions(axrb_runtime_input_smoke PRIVATE AXRB_INPUT_FIXTURE)
  target_link_libraries(axrb_runtime_input_smoke PRIVATE axrb_protocol)
  add_test(NAME axrb_runtime_transforms_hand_joints COMMAND axrb_runtime_input_smoke)
endif()
