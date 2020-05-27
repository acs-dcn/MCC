
include_directories(${CMAKE_SOURCE_DIR})

add_executable(master mcc_master.cc ../../proto/mcc.pb.cc)
add_executable(worker mcc_worker.cc ../../proto/mcc.pb.cc)

target_link_libraries(master infnet)
target_link_libraries(worker infnet)

add_custom_command(
  TARGET worker 
  POST_BUILD
#  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/ip_setup.sh ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -r ${CMAKE_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)

