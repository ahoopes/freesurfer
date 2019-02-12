project(nifti)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(nifti STATIC nifti1_io.c znzlib.c)
