# Адаптеры KDE/Wayland. Единственное место в проекте, знающее про
# LayerShellQt, D-Bus и прочие платформенные детали.

add_library(openpet-platform-kde STATIC
    src/overlaysurface.cpp
    src/inputregion.cpp
    include/overlaysurface.h
    include/inputregion.h
)

target_include_directories(openpet-platform-kde PUBLIC include)

target_link_libraries(openpet-platform-kde PUBLIC
    Qt6::Quick
    LayerShellQt::Interface
)

add_library(openpet::platform-kde ALIAS openpet-platform-kde)
