

infgen_add_app(mcc
    NAME mcc
    SOURCES client.cc
)
add_custom_command(
  TARGET mcc
  POST_BUILD
#  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/ip_setup.sh ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -r ${PROJECT_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)

