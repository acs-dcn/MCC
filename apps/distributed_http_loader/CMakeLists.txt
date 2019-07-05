
include_directories(${PROJECT_SOURCE_DIR})

add_executable(http_worker loader.cc ../../proto/http.pb.cc)
add_executable(http_controller controller.cc ../../proto/http.pb.cc)

target_link_libraries(http_worker infnet)
target_link_libraries(http_controller infnet)

