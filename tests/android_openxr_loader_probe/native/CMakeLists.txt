cmake_minimum_required(VERSION 3.22)

project(axrb_openxr_loader_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(openxr_loader_probe SHARED openxr_loader_probe.cpp)

target_compile_definitions(openxr_loader_probe PRIVATE XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES)

add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
  IMPORTED_LOCATION
    ${OPENXR_AAR_DIR}/extracted/jni/${ANDROID_ABI}/libopenxr_loader.so
)

target_include_directories(openxr_loader_probe
  PRIVATE
    ${OPENXR_AAR_DIR}/extracted/prefab/modules/headers/include
)

target_link_libraries(openxr_loader_probe
  PRIVATE
    openxr_loader
    android
    EGL
    GLESv3
    log
)
