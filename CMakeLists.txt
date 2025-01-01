cmake_minimum_required(VERSION 3.12)

project(SecureGateway)

option(BAREMETAL "Baremetal build" OFF)
option(USE_XOR "Use XOR for encryption" OFF)
option(USE_CONSOLE "Use console for logging" OFF)

if(BAREMETAL)
    message(STATUS "Secure MAVLink Gateway baremetal build")
else()
    message(STATUS "Secure MAVLink Gateway Linux build")
endif()

if (NOT BAREMETAL)
set(CMAKE_CXX_STANDARD 11)
endif()

set(CMAKE_C_STANDARD 11)

include_directories(
    lib
    inc
    inc/mavlink/ardupilotmega
    inc/mavlink
)

include(FindPython3)

set(MAVGEN  ${CMAKE_COMMAND} -E env "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/mavlink:$ENV{PYTHONPATH}"
            ${Python3_EXECUTABLE} -m pymavlink.tools.mavgen)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mavlink.h.tstamp
    COMMAND ${MAVGEN} --lang=C --wire-protocol 2.0
        --output=${CMAKE_CURRENT_SOURCE_DIR}/inc/mavlink
        ${CMAKE_CURRENT_SOURCE_DIR}/mavlink/message_definitions/v1.0/ardupilotmega.xml
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/mavlink.h.tstamp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mavlink/message_definitions/v1.0/ardupilotmega.xml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/mavlink
    COMMENT "Generating MAVLink headers"
    VERBATIM
)

add_custom_target(
    mavlink_headers
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mavlink.h.tstamp
)

if (USE_XOR)
    add_definitions(-DUSE_XOR)
    set(TRANSFORMER_SRC lib/transformer_xor.c)
else()
    set(TRANSFORMER_SRC lib/transformer_none.c)
endif()


if (BAREMETAL)

add_compile_options(-nostdlib -nostdinc -ffreestanding -fno-builtin
    -fno-common -fno-exceptions -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-strict-aliasing
    -fno-omit-frame-pointer -fno-strict-overflow -fno-delete-null-pointer-checks
    -fno-PIE -fno-pic -fno-pie -fno-pic -fno-stack-protector -fno-unwind-tables
    -fno-asynchronous-unwind-tables -fno-exceptions -fno-omit-frame-pointer
    -fno-delete-null-pointer-checks
)


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
    lib/source_stdio.c)

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
    lib/source_tcpout.c
    lib/source_udp.c
    lib/source_stdio.c
    lib/source_uart.c
    )

endif()

option(HAS_CERTIKOS_THINROS "Build with CertiKOS ThinROS / User" OFF)
option(HAS_CERTIKOS_UART    "Build with CertiKOS UART / User" OFF)

if (HAS_CERTIKOS_THINROS)

    message(STATUS "Building with CertiKOS ThinROS / User")
    add_definitions(-D_CERTIKOS_)
    include_directories(${USER_DIR}/include)
    link_libraries(user_lib ringleader)

    list(APPEND source_sink_list
        lib/source_thinros_certikos.c
        lib/source_certikos_uart.c
        lib/source_ringleader.c
        lib/source_partee.c
        )
endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function")
add_compile_options(-Wno-address-of-packed-member)
add_compile_options(-Wno-missing-field-initializers)
add_compile_options(${EXTRA_C_FLAGS})

if (USE_CONSOLE)
    add_definitions(-DUSE_CONSOLE)
    message(STATUS "USE_CONSOLE: yes")
    list(APPEND plugins
        lib/plugins/console.c
        )
endif()


add_library(gateway
    STATIC
    lib/secure_gateway.c
    lib/route_table.c
    lib/security_policies.c
    ${plugins}
    ${source_sink_list}
    ${TRANSFORMER_SRC}
    )

add_dependencies(gateway mavlink_headers)

add_executable(secure_gateway
    main.c
)

target_link_libraries(secure_gateway
    PRIVATE
    gateway
)

add_executable(main-sim
    test/main-sim.c
)

target_link_libraries(main-sim
    PRIVATE
    gateway
)

add_executable(main-dump
    test/main-dump.c
)

target_link_libraries(main-dump
    PRIVATE
    gateway
)

add_executable(main-xor
    test/main-xor.c
)

target_link_libraries(main-xor
    PRIVATE
    gateway
)

add_executable(tcp_bridge
    tools/tcp_bridge.cc)

