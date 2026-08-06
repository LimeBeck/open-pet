qt_add_executable(open-pet
    src/main.cpp
    src/corebridge.cpp
    src/corebridge.h
    src/petviewmodel.cpp
    src/petviewmodel.h
    src/mockeventsource.cpp
    src/mockeventsource.h
    src/settings.cpp
    src/settings.h
)

target_link_libraries(open-pet PRIVATE
    Qt6::Quick
    # QSystemTrayIcon и QMenu живут в Widgets: трей — единственная причина
    # зависимости, но без него из приложения нечем выйти (§4.1).
    Qt6::Widgets
    openpet::core
    openpet::platform-kde
    openpet::ui
    openpet-uiplugin
)

# Статическая библиотека Rust тянет за собой std, которому нужны
# системные библиотеки.
target_link_libraries(open-pet PRIVATE pthread dl m)

install(TARGETS open-pet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES data/org.openpet.DesktopPet.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
