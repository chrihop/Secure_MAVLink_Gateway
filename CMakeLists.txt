cmake_minimum_required(VERSION 3.0)

project(SecureGateway)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

add_subdirectory(mavlink)

include_directories(
    inc
    inc/mavlink
)

include(FindPython3)

set(MAVGEN  ${CMAKE_COMMAND} -E env "PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/mavlink"
            ${Python3_EXECUTABLE} -m pymavlink.tools.mavgen)

add_custom_command(
    OUTPUT inc/mavlink/mavlink.h
    COMMAND ${MAVGEN} --lang=C --wire-protocol 2.0
        --output=${CMAKE_SOURCE_DIR}/inc/mavlink
        ${CMAKE_SOURCE_DIR}/mavlink/message_definitions/v1.0/ardupilotmega.xml
    DEPENDS ${CMAKE_SOURCE_DIR}/mavlink/message_definitions/v1.0/ardupilotmega.xml
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating MAVLink headers"
    VERBATIM
)

add_custom_target(
    mavlink_headers
    DEPENDS inc/mavlink/mavlink.h
)

if (BAREMETAL)

add_compile_options(-nostdlib -nostdinc -ffreestanding -fno-builtin
    -fno-common -fno-exceptions -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-strict-aliasing
    -fno-omit-frame-pointer -fno-strict-overflow -fno-delete-null-pointer-checks
    -fno-PIE -fno-pic -fno-pie -fno-pic -fno-stack-protector -fno-unwind-tables
    -fno-asynchronous-unwind-tables -fno-exceptions -fno-omit-frame-pointer
    -fno-delete-null-pointer-checks)

execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgcc.a
    OUTPUT_VARIABLE C_LIBGCC_FILE
    OUTPUT_STRIP_TRAILING_WHITESPACE)

link_libraries(
    ${C_LIBGCC_FILE}
    -nostdlib -nostdinc -ffreestanding -fno-builtin
    -fno-common -fno-exceptions -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-strict-aliasing
    -fno-omit-frame-pointer -fno-strict-overflow -fno-delete-null-pointer-checks
    -fno-PIE -fno-pic -fno-pie -fno-pic -fno-stack-protector -fno-unwind-tables
    -fno-asynchronous-unwind-tables -fno-exceptions -fno-omit-frame-pointer
    -fno-delete-null-pointer-checks
)

include_directories(
    inc/baremetal
)

else()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_compile_definitions(
    _STD_LIBC_
)

endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function")
add_compile_options(-Wno-address-of-packed-member)

add_executable(secure_gateway
    main.c
)

