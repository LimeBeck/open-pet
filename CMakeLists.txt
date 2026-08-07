cmake_minimum_required(VERSION 3.25)

project(open-pet
    VERSION 0.1.0
    DESCRIPTION "Desktop AI Pet for KDE Plasma Wayland"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Widgets DBus Network)
find_package(LayerShellQt REQUIRED)
find_package(KF6IdleTime REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(core)
add_subdirectory(platform/kde-wayland)
add_subdirectory(ui/qml)
add_subdirectory(apps/desktop-kde)

# Тесты домена живут в Rust и запускаются через cargo: они не требуют
# ни Qt, ни Wayland, ни дисплея.
enable_testing()
add_test(
    NAME core-tests
    COMMAND cargo test --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/core/Cargo.toml
)
