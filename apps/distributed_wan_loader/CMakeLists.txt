
include_directories(${PROJECT_SOURCE_DIR})

add_executable(wan_worker loader.cc ../../proto/wan.pb.cc)
add_executable(wan_controller controller.cc ../../proto/wan.pb.cc)

target_link_libraries(wan_worker infnet)
target_link_libraries(wan_controller infnet)

add_custom_command(
  TARGET wan_worker 
  POST_BUILD
#  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/ip_setup.sh ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -r ${CMAKE_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)
