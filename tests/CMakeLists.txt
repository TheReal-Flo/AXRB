add_test(NAME axrb_host_bridge_starts COMMAND axrb-host-bridge)

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

add_subdirectory(hello_xr)
