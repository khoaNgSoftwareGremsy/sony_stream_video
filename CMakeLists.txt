cmake_minimum_required(VERSION 3.5)

project(RemoteCli LANGUAGES CXX)

### Append project cmake script dir ###
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

### Enumerate project files ###
include(enum_cli_hdr)
include(enum_cli_src)
include(enum_crsdk_hdr)

### Define output target ###
set(remotecli "${PROJECT_NAME}")

#### INCLUDE         ##########################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${MODEL_INCLUDES}
                    ${EXIV2_INCLUDES}
                    ${MODEL_RESULT_INCLUDE}
                    ${OPENCV_INCLUDES}
                    ${GSTREAMER_INCLUDES}
                    ${GST_RTSP_SERVER_INCLUDES}
                    ${OCLEA_PIPELINE_UTIL_INCLUDES}
                    ${WEBRTC_GST_DEPS}
                    ${__cli_hdr_dir}
                    ${__crsdk_hdr_dir}
                    )

add_executable(${remotecli}
    ${cli_hdrs}
    ${cli_srcs}
    ${crsdk_hdrs}
)

find_package( Threads )

## Specify char is signed-char to fix mismatch with Raspbian
target_compile_options(${remotecli}
    PRIVATE
        -fsigned-char
)

target_include_directories(${remotecli}
    PRIVATE
        ${crsdk_hdr_dir} # defined in enum script
)

### Configure external library directories ###
set(ldir ${CMAKE_CURRENT_SOURCE_DIR}/external)
set(cr_ldir ${ldir}/crsdk)

### Link CRSDK library
find_library(camera_remote Cr_Core HINTS ${cr_ldir})
target_link_libraries(${remotecli}
    PRIVATE
        ${camera_remote}
        ${CMAKE_THREAD_LIBS_INIT}
        ${WEBRTC_GST_DEPS}
        ${OPENCV_DEPS}
        ${GST_DEPS}
        ${EXIV2_DEPS}
        ${GLIB_DEPS}
        ${PROTOBUF_DEPS}
        ${OCLEA_PIPELINE_UTIL_DEPS}
)

set_target_properties(${remotecli} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)


### Linux specific configuration ###
if(UNIX)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
            # Must use std::experimental namespace if older than GCC8
            message("[${PROJECT_NAME}] GCC version less than 8. Using std::experimental namespace.")
            target_compile_definitions(${remotecli} PRIVATE USE_EXPERIMENTAL_FS)
        endif()

        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
            # Must explicitly link separate std::filesystem if older than GCC9
            message("[${PROJECT_NAME}] GCC version less than 9. Explicitly linking separate std::filesystem library.")
            target_link_libraries(${remotecli} PUBLIC stdc++fs)
        endif()
    endif()
endif(UNIX)

## Copy required library binaries
if(WIN32)
    add_custom_command(TARGET ${remotecli} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${cr_ldir} $<TARGET_FILE_DIR:${remotecli}>
    )
endif(WIN32)

if(UNIX AND NOT APPLE)
    add_custom_command(TARGET ${remotecli} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${cr_ldir} $<TARGET_FILE_DIR:${remotecli}>
    )
endif(UNIX AND NOT APPLE)




## Install application
## '.' means, install to the root directory of CMAKE_INSTALL_PREFIX
install(TARGETS ${remotecli} DESTINATION /usr/bin/tsdk_sony)
install(DIRECTORY ${cr_ldir}/ DESTINATION /usr/bin/tsdk_sony)
