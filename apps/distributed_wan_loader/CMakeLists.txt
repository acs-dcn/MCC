
include_directories(${PROJECT_SOURCE_DIR})

add_executable(wan_worker loader.cc ../../proto/wan.pb.cc)
add_executable(wan_controller controller.cc ../../proto/wan.pb.cc)

target_link_libraries(wan_worker infnet)
target_link_libraries(wan_controller infnet)

