# Общий QML UI. Платформенных деталей здесь нет: тот же модуль должен
# подойти будущим хостам под Windows и macOS (гипотеза 4 из §1).

qt_add_library(openpet-ui STATIC)

qt_add_qml_module(openpet-ui
    URI OpenPet.Ui
    VERSION 1.0
    QML_FILES
        Main.qml
        Pet.qml
)

add_library(openpet::ui ALIAS openpet-ui)
