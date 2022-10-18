cmake_minimum_required(VERSION 3.0)

project(SecureGateway)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

#add_subdirectory(mavlink)

include_directories(
    lib
    inc
    inc/mavlink
    inc/mavlink/ardupilotmega
)

include(FindPython3)

set(MAVGEN  ${CMAKE_COMMAND} -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/mavlink:$ENV{PYTHONPATH}"
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

add_link_options(
    -nostdlib -nostdinc -ffreestanding -fno-builtin
    -fno-common -fno-exceptions -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-strict-aliasing
    -fno-omit-frame-pointer -fno-strict-overflow -fno-delete-null-pointer-checks
    -fno-PIE -fno-pic -fno-pie -fno-pic -fno-stack-protector -fno-unwind-tables
    -fno-asynchronous-unwind-tables -fno-exceptions -fno-omit-frame-pointer
    -fno-delete-null-pointer-checks
)

execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgcc.a
    OUTPUT_VARIABLE C_LIBGCC_FILE
    OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libm.a
    OUTPUT_VARIABLE C_LIBM_FILE
    OUTPUT_STRIP_TRAILING_WHITESPACE)

link_libraries(
    ${C_LIBGCC_FILE}
    ${C_LIBM_FILE}
)

include_directories(
    inc/baremetal
)

set (source_sink_list
    lib/secure_uart.c)

else()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
    add_compile_options(-fsanitize=address,undefined -rdynamic)
    add_link_options(-fsanitize=address,undefined -rdynamic)
endif()

add_compile_definitions(
    _STD_LIBC_
)

link_libraries(
    pthread
)

set (source_sink_list
    lib/source_tcp.c
    lib/source_udp.c
    lib/source_stdio.c
    lib/source_uart.c
    )

endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function")
add_compile_options(-Wno-address-of-packed-member)
add_compile_options(-Wno-missing-field-initializers)

add_executable(secure_gateway
    main.c
    lib/secure_gateway.c
    lib/route_table.c
    lib/security_policies.c
    ${source_sink_list}
)

