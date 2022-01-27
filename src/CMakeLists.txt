set(LIBRARY_NAME "dmon")

set(Header_Files "../dmon.h")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(BUILD_DMON_EXTRA "Build dmon extra" ON)
    list(APPEND Header_Files "../dmon_extra.h")
endif (CMAKE_SYSTEM_NAME STREQUAL "Linux")

source_group("Header Files" FILES "${Header_Files}")

add_library("${LIBRARY_NAME}" INTERFACE "${Header_Files}")

target_include_directories(
        "${LIBRARY_NAME}"
        INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

if (APPLE)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(CORE_SERVICES CoreServices REQUIRED)
    target_link_libraries("${LIBRARY_NAME}" INTERFACE "${CORE_FOUNDATION}" "${CORE_SERVICES}")
endif (APPLE)

set_target_properties(
        "${LIBRARY_NAME}"
        PROPERTIES
        LINKER_LANGUAGE
        C
)

# install rules

# setup the version numbering
set_property(TARGET "${LIBRARY_NAME}" PROPERTY VERSION "1.0.0")
set_property(TARGET "${LIBRARY_NAME}" PROPERTY SOVERSION "1")

set(installable_libs "${LIBRARY_NAME}" "${PROJECT_NAME}_compiler_flags")
install(FILES ${Header_Files} DESTINATION "include")

if (TARGET "${DEPENDANT_LIBRARY}")
    list(APPEND installable_libs "${DEPENDANT_LIBRARY}")
endif (TARGET "${DEPENDANT_LIBRARY}")
