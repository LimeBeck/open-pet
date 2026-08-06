cmake_minimum_required(VERSION 3.25)
project(openpet_spike_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

# Сборка Rust вызовом cargo напрямую, без Corrosion и прочих посредников.
# Для спайка это ответ на вопрос «сколько стоит интеграция сборки»:
# оказалось — один custom command и одна imported library.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core)
set(CORE_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust)
set(CORE_LIB ${CORE_TARGET_DIR}/release/libopenpet_core_spike.a)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CORE_DIR}/src/*.rs)

add_custom_command(
    OUTPUT ${CORE_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CORE_TARGET_DIR}
            cargo build --release --manifest-path ${CORE_DIR}/Cargo.toml
    DEPENDS ${CORE_SOURCES} ${CORE_DIR}/Cargo.toml
    COMMENT "Сборка Rust-ядра"
    VERBATIM
)

add_custom_target(openpet-core-build DEPENDS ${CORE_LIB})

add_library(openpet-core STATIC IMPORTED GLOBAL)
set_target_properties(openpet-core PROPERTIES
    IMPORTED_LOCATION ${CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_DIR}/include
)
add_dependencies(openpet-core openpet-core-build)

add_executable(openpet-spike-bridge
    host/main.cpp
    host/corebridge.cpp
    host/corebridge.h
)

# staticlib из Rust тянет за собой std, которому нужны системные библиотеки.
target_link_libraries(openpet-spike-bridge PRIVATE
    Qt6::Core
    openpet-core
    pthread
    dl
    m
)
