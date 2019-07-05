
set(UTIL_FLD "${PROJECT_SOURCE_DIR}/mtcp/util")

include_directories("${UTIL_FLD}/include")

set(UTIL_OBJ 
  "${UTIL_FLD}/http_parsing.o"
  "${UTIL_FLD}/netlib.o"
  "${UTIL_FLD}/tdate_parse.o"
)

add_executable(wget ${UTIL_OBJ} epwget.cc)

target_compile_options(wget PUBLIC -g -O3 -Wall -DDBGFUNC)

target_link_libraries(wget infnet pthread gmp)

