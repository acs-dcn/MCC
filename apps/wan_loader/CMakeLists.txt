

infgen_add_app(wan_loader
    NAME wan_loader
    SOURCES wan_loader.cc
)
add_custom_command(
  TARGET wan_loader
  POST_BUILD
  COMMAND cp -r ${CMAKE_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)

