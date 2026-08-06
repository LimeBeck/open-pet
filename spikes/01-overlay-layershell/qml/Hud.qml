import QtQuick

// Панель с цифрами. Существует только ради ручной проверки: её площадь входит
// в input region, поэтому для замеров региона её отключают через OPENPET_HUD=0.
Rectangle {
    id: hud

    implicitWidth: 250
    implicitHeight: column.implicitHeight + 16
    radius: 6
    color: "#d9101418"
    border.color: "#3affffff"
    border.width: 1

    Column {
        id: column
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        Text {
            text: spike.modeName
            color: "#ffd479"
            font.pixelSize: 12
            font.bold: true
        }

        Text {
            text: "FPS " + spike.fps.toFixed(1)
                  + "   CPU " + spike.cpuPercent.toFixed(1) + "%"
                  + "   RSS " + spike.rssMib + " МиБ"
            color: "#e6e6e6"
            font.pixelSize: 11
        }

        Text {
            text: "регион: " + spike.regionRects + " прямоуг., "
                  + spike.regionBuildMs.toFixed(2) + " мс"
            color: "#e6e6e6"
            font.pixelSize: 11
        }

        Text {
            text: "кликов принято: " + spike.clicks
            color: "#e6e6e6"
            font.pixelSize: 11
        }

        Text {
            text: spike.note
            color: "#9fd4ff"
            font.pixelSize: 10
            width: parent.width
            wrapMode: Text.Wrap
            visible: text.length > 0
        }

        Text {
            text: "ПКМ — следующая стратегия"
            color: "#8a8a8a"
            font.pixelSize: 10
        }
    }
}
