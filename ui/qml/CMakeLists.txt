# Общий QML UI. Платформенных деталей здесь нет: тот же модуль должен
# подойти будущим хостам под Windows и macOS (гипотеза 4 из §1).

qt_add_library(openpet-ui STATIC)

# Встроенный питомец вшивается в исполняемый файл: §4.1 требует, чтобы
# приложение показывало питомца сразу, без импорта пакета. Импортируемые
# Pet Pack будут грузиться с диска — это M5.
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/builtin-pet/lime.png
    PROPERTIES QT_RESOURCE_ALIAS lime.png
)

qt_add_qml_module(openpet-ui
    URI OpenPet.Ui
    VERSION 1.0
    QML_FILES
        Main.qml
        Pet.qml
        Bubble.qml
    RESOURCES
        ${CMAKE_SOURCE_DIR}/assets/builtin-pet/lime.png
)

add_library(openpet::ui ALIAS openpet-ui)
