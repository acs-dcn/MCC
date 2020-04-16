cmake_minimum_required (VERSION 3.5.0)
project (infgen)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

find_package(Boost 1.53 REQUIRED COMPONENTS program_options)
find_package(fmt 5.0 REQUIRED)
find_package(dpdk REQUIRED)

set(MTCP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/mtcp/mtcp/include")
set(MTCP_LIBRARY_PATH "${PROJECT_SOURCE_DIR}/mtcp/mtcp/lib")
set(DPDK_LIBRARY_PATH "$ENV{RTE_SDK}/$ENV{RTE_TARGET/lib}")
set(AES_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/aes")
set(AES_LIBRARY_PATH "${PROJECT_SOURCE_DIR}/lib")

set (CMAKE_BUILD_TYPE
  "${CMAKE_BUILD_TYPE}"
  CACHE
  STRING
  "Choose the type of build, options are: None Debug Release Dev."
  FORCE
)

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE "Release")
endif()

add_library(infnet STATIC 
  src/log.cc
  src/timer.cc
  src/reactor.cc
  src/resource.cc
  src/application.cc
  src/smp.cc
  src/pollfd.cc
  src/epoll.cc
  src/posix_connector.cc
  src/posix_connection.cc
  src/mtcp_connector.cc
  src/mtcp_connection.cc
  src/mepoll.cc
  src/io_sched.cc
  src/tcp_server.cc
	src/ssl_layer.cc
)

target_include_directories(infnet
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MTCP_INCLUDE_DIR}
		${AES_INCLUDE_DIR}
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_directories(infnet
  PUBLIC
    ${MTCP_LIBRARY_PATH}
    ${DPDK_LIBRARY_PATH}
    ${AES_LIBRARY_PATH}
)

target_compile_features(infnet PUBLIC cxx_std_17)
target_compile_options(infnet 
  PUBLIC
    -fvisibility=hidden
    -Wall
    -Werror
    -Wno-error=deprecated-declarations
		-g
#-DAES_GCM
)

target_link_libraries(infnet 
  PUBLIC 
    fmt 
    pthread 
    Boost::boost
    Boost::program_options
    mtcp 
    gmp
		aes
    #numa
    #dl
    #dpdk
  INTERFACE
    protobuf
)

#add_subdirectory(tests)
add_subdirectory(apps)


