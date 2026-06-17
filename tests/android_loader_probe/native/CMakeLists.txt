cmake_minimum_required(VERSION 3.22)

project(axrb_loader_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(loader_probe SHARED loader_probe.cpp)

target_include_directories(loader_probe
  PRIVATE
    ${AXRB_ROOT}/android-runtime
)

target_link_libraries(loader_probe
  PRIVATE
    android
    log
    dl
)
