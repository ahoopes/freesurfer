project(gifti)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/packages/nifti
    ${CMAKE_SOURCE_DIR}/packages/expat
)

set(GIFTI_SOURCES gifti_io.c gifti_xml.c)

add_library(gifti STATIC ${GIFTI_SOURCES})

add_executable(gifti_tool gifti_tool.c ${GIFTI_SOURCES})
target_link_libraries(gifti_tool nifti expat ${ZLIB_LIBRARIES})
install(TARGETS gifti_tool DESTINATION bin)
