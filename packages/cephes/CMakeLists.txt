project(cephes)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES ${SOURCES}
  bdtr.c
  chdtr.c
  const.c
  fdtr.c
  gamma.c
  igam.c
  igami.c
  incbet.c
  incbi.c
  mtherr.c
  ndtr.c
  ndtri.c
  polevl.c
  stdtr.c
)

add_library(cephes STATIC ${SOURCES})
