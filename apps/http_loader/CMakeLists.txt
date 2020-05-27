infgen_add_app(http_loader
    NAME http_loader
    SOURCES http_loader.cc
)
add_custom_command(
  TARGET http_loader
  POST_BUILD
  COMMAND cp -r ${CMAKE_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR}
)
