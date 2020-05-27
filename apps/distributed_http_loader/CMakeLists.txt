
include_directories(${PROJECT_SOURCE_DIR})

add_executable(http_worker loader.cc ../../proto/http.pb.cc)
add_executable(http_controller controller.cc ../../proto/http.pb.cc)

target_link_libraries(http_worker infnet)
target_link_libraries(http_controller infnet)

add_custom_command(
  TARGET http_worker
  POST_BUILD
  COMMAND cp -r ${CMAKE_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)
