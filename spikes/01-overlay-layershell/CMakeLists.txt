cmake_minimum_required(VERSION 3.25)
project(openpet_spike_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(LayerShellQt REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

# Файлы QML лежат в подкаталоге, а в модуле должны оказаться в корне —
# иначе Qt требует отдельный qmldir на каждый каталог (политика QTP0004).
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/Pet.qml PROPERTIES QT_RESOURCE_ALIAS Pet.qml)
set_source_files_properties(qml/Hud.qml PROPERTIES QT_RESOURCE_ALIAS Hud.qml)

qt_add_executable(openpet-spike-overlay
    src/main.cpp
    src/spikecontroller.cpp
    src/spikecontroller.h
    src/inputregion.cpp
    src/inputregion.h
)

qt_add_qml_module(openpet-spike-overlay
    URI OpenPetSpike
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Pet.qml
        qml/Hud.qml
)

target_link_libraries(openpet-spike-overlay PRIVATE
    Qt6::Quick
    LayerShellQt::Interface
)
