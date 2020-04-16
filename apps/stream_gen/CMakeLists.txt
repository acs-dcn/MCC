infgen_add_app(stream_gen
    NAME stream_gen
    SOURCES stream_gen.cc
)

add_custom_command(
  TARGET stream_gen
  POST_BUILD
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/dst-sample.txt ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/pcapfiles ${CMAKE_CURRENT_BINARY_DIR}
)

set(PCAP_NET_INCLUDE_DIR "/usr/local/include")
set(NIDS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/stream_gen/libnids-1.24/src")
set(NIDS_LIBRARY_PATH "/usr/local/lib")
set(STREAMGEN_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/stream_gen/include")
set(STREAMGEN_LIBRARY_PATH "${PROJECT_SOURCE_DIR}/lib")

target_include_directories(stream_gen
  PUBLIC
    ${STREAMGEN_INCLUDE_DIR}
    ${PCAP_NET_INCLUDE_DIR}
    ${NIDS_INCLUDE_DIR}
)

target_link_directories(stream_gen
  PUBLIC
    ${STREAMGEN_LIBRARY_PATH}
    ${NIDS_LIBRARY_PATH}
)

target_link_libraries(stream_gen
  PUBLIC
    sgen
    nids
    glib-2.0
    pcap
    net
    gthread-2.0
    nsl
)
