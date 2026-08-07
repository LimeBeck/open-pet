# Адаптеры KDE/Wayland. Единственное место в проекте, знающее про
# LayerShellQt, D-Bus и прочие платформенные детали.

add_library(openpet-platform-kde STATIC
    src/overlaysurface.cpp
    src/inputregion.cpp
    src/eventsource.cpp
    src/idleadapter.cpp
    src/poweradapter.cpp
    src/sessionadapter.cpp
    src/mediaadapter.cpp
    src/notificationadapter.cpp
    src/activeappadapter.cpp
    include/overlaysurface.h
    include/inputregion.h
    include/eventsource.h
    include/idleadapter.h
    include/poweradapter.h
    include/sessionadapter.h
    include/mediaadapter.h
    include/notificationadapter.h
    include/activeappadapter.h
)

target_include_directories(openpet-platform-kde PUBLIC include)

target_link_libraries(openpet-platform-kde PUBLIC
    Qt6::Quick
    Qt6::DBus
    LayerShellQt::Interface
    KF6::IdleTime
)

add_library(openpet::platform-kde ALIAS openpet-platform-kde)

# KWin-скрипт ставится в каталог пользователя отдельным действием, а не при
# установке приложения: §FR-3 требует, чтобы интеграция была явно
# устанавливаемой. Здесь он только кладётся в общий каталог данных, откуда
# приложение предложит его установить.
install(DIRECTORY kwin-script/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/open-pet/kwin-script
)
