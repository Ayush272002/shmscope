set(SHMSCOPE_SANITIZE "off" CACHE STRING
    "Sanitizer mode: off, address (ASan+UBSan), or thread (TSan+UBSan)")
set_property(CACHE SHMSCOPE_SANITIZE PROPERTY STRINGS off address thread)

add_library(shmscope_sanitizers INTERFACE)
add_library(shmscope::sanitizers ALIAS shmscope_sanitizers)

if(NOT SHMSCOPE_SANITIZE STREQUAL "off")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(WARNING
            "SHMSCOPE_SANITIZE=${SHMSCOPE_SANITIZE} but "
            "${CMAKE_CXX_COMPILER_ID} is unsupported; ignoring.")
    elseif(SHMSCOPE_SANITIZE STREQUAL "address")
        set(_shm_sanitize_flags
            -fsanitize=address,undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
    elseif(SHMSCOPE_SANITIZE STREQUAL "thread")
        set(_shm_sanitize_flags
            -fsanitize=thread,undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
    else()
        message(FATAL_ERROR
            "SHMSCOPE_SANITIZE must be off, address or thread, "
            "got '${SHMSCOPE_SANITIZE}'")
    endif()

    if(_shm_sanitize_flags)
        target_compile_options(shmscope_sanitizers INTERFACE ${_shm_sanitize_flags})
        target_link_options(shmscope_sanitizers INTERFACE ${_shm_sanitize_flags})
        message(STATUS "Sanitizers: ${SHMSCOPE_SANITIZE}")
    endif()
endif()
