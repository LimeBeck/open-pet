# Адаптеры KDE/Wayland. Единственное место в проекте, знающее про
# LayerShellQt, D-Bus и прочие платформенные детали.

add_library(openpet-platform-kde STATIC
    src/overlaysurface.cpp
    src/inputregion.cpp
    src/eventsource.cpp
    src/idleadapter.cpp
    src/poweradapter.cpp
    src/sessionadapter.cpp
    include/overlaysurface.h
    include/inputregion.h
    include/eventsource.h
    include/idleadapter.h
    include/poweradapter.h
    include/sessionadapter.h
)

target_include_directories(openpet-platform-kde PUBLIC include)

target_link_libraries(openpet-platform-kde PUBLIC
    Qt6::Quick
    Qt6::DBus
    LayerShellQt::Interface
    KF6::IdleTime
)

add_library(openpet::platform-kde ALIAS openpet-platform-kde)
