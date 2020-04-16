
function (infgen_add_app)
    cmake_parse_arguments (parsed_args
      ""
      "NAME"
      "SOURCES"
      ${ARGN}
    )
    if (parsed_args_SOURCES)
      set (executable_target ${parsed_args_NAME})
      add_executable (${executable_target} ${parsed_args_SOURCES})
      target_link_libraries (${executable_target} 
        PRIVATE infnet)
    endif()
endfunction (infgen_add_app)

#set(SGEN_LIB_FLAGS "-lnids -lglib-2.0 -lpcap -lnet -lgthread-2.0 -lnsl")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SGEN_LIB_FLAGS}")
file(STRINGS "$ENV{RTE_SDK}/$ENV{RTE_TARGET}/lib/ldflags.txt" DPDK_LIB_FLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DPDK_LIB_FLAGS}")

add_subdirectory(mcc)
add_subdirectory(distributed_mcc)
add_subdirectory(http_loader)
#add_subdirectory(echo_client)
add_subdirectory(distributed_http_loader)
# @wuwenqing
add_subdirectory(wan_loader)
add_subdirectory(distributed_wan_loader)
add_subdirectory(stream_gen)

