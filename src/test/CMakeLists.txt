foreach(name "" "-incremental")
    set(EXEC_NAME "${PROJECT_NAME}_test${name}")

    set(Source_Files "../../test${name}.c")
    source_group("${EXEC_NAME} Source Files" FILES "${Source_Files}")

    add_executable("${EXEC_NAME}" "${Source_Files}")

    target_link_libraries("${EXEC_NAME}" PUBLIC "${LIBRARY_NAME}")
    if (APPLE)
        find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
        find_library(CORE_SERVICES CoreServices REQUIRED)
        target_link_libraries("${EXEC_NAME}" PRIVATE "${CORE_FOUNDATION}" "${CORE_SERVICES}")
    endif (APPLE)
    set_target_properties(
            "${EXEC_NAME}"
            PROPERTIES
            LINKER_LANGUAGE
            C
    )
    add_test(NAME "${EXEC_NAME}" COMMAND "${EXEC_NAME}")
endforeach (name "" "-incremental")
