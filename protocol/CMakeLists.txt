add_library(axrb_protocol STATIC
  image_transport.cpp
  transport_tcp.cpp
  transport_shm.cpp
  clock_sync.cpp
)

target_include_directories(axrb_protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
  target_link_libraries(axrb_protocol PUBLIC ws2_32)
endif()
