
function (infgen_add_test)
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
endfunction (infgen_add_test)

#add_executable(possion_gen possion_gen.cc)

file(STRINGS "$ENV{RTE_SDK}/$ENV{RTE_TARGET}/lib/ldflags.txt" DPDK_LIB_FLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DPDK_LIB_FLAGS}")


infgen_add_test (log
  NAME log_test
  SOURCES log_test.cc
)

infgen_add_test (conn
  NAME conn_test
  SOURCES conn_test.cc
)

infgen_add_test (recon
  NAME reconnect_test
  SOURCES reconnect_test.cc
)

infgen_add_test(smp
  NAME smp_test
  SOURCES smp_test.cc
)

infgen_add_test(distributor
  NAME distributed_test
  SOURCES distributed_test.cc
)

infgen_add_test(timer
  NAME timer_test
  SOURCES timer_test.cc
)

infgen_add_test(delay
  NAME delay_test
  SOURCES delay_test.cc
)

infgen_add_test(ip
  NAME ip_test
  SOURCES ip_test.cc
)

infgen_add_test(burst
  NAME burst_test
  SOURCES burst_test.cc
)

infgen_add_test(worker
  NAME worker_test
  SOURCES ${CMAKE_SOURCE_DIR}/proto/mcc.pb.cc proto_worker.cc 
)

infgen_add_test(master
  NAME master_test
  SOURCES ${CMAKE_SOURCE_DIR}/proto/mcc.pb.cc proto_master.cc 
)

infgen_add_test(CBR
  NAME CBR_test
  SOURCES CBR_test.cc
)

infgen_add_test(accurate
  NAME accurate_test
  SOURCES accurate_ts.cc
)

infgen_add_test(possion
  NAME  possion_test
  SOURCES possion_traffic_test.cc
)
