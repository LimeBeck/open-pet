# Сборка Rust-ядра вызовом cargo напрямую.
#
# Corrosion и прочие посредники не нужны: на спайке ADR-001 выяснилось,
# что интеграция укладывается в один custom command и одну imported library.

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CORE_PROFILE dev)
    set(CORE_PROFILE_DIR debug)
else()
    set(CORE_PROFILE release)
    set(CORE_PROFILE_DIR release)
endif()

set(CORE_LIB ${CORE_TARGET_DIR}/${CORE_PROFILE_DIR}/libopenpet_core.a)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CORE_DIR}/src/*.rs)

# Встроенный питомец вшит в ядро через include_str!/include_bytes!, поэтому
# его правка обязана пересобирать ядро. Без этой зависимости манифест менялся,
# а библиотека оставалась прежней: питомец в окне был новый, а раскладка
# и резерв под движение — от старого пакета. Поймано на живой проверке.
set(BUILTIN_PET
    ${CMAKE_SOURCE_DIR}/assets/builtin-pet/manifest.json
    ${CMAKE_SOURCE_DIR}/assets/builtin-pet/lime.png
)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

add_custom_command(
    OUTPUT ${CORE_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CORE_TARGET_DIR}
            ${CARGO_EXECUTABLE} build --profile ${CORE_PROFILE}
            --manifest-path ${CORE_DIR}/Cargo.toml
    DEPENDS ${CORE_SOURCES} ${CORE_DIR}/Cargo.toml ${BUILTIN_PET}
    COMMENT "Сборка Rust-ядра (${CORE_PROFILE})"
    VERBATIM
)

add_custom_target(openpet-core-build DEPENDS ${CORE_LIB})

add_library(openpet::core STATIC IMPORTED GLOBAL)
set_target_properties(openpet::core PROPERTIES
    IMPORTED_LOCATION ${CORE_LIB}
    # Контракт границы живёт отдельно от реализации: заголовок описывает,
    # что видит хост, и правится осознанно (ADR-001, правило 6).
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/platform/contracts/include
)
add_dependencies(openpet::core openpet-core-build)
