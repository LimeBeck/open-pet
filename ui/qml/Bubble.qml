import QtQuick

// Пузырь с репликой (§FR-6).
//
// Закрывается сам по таймеру ViewModel и по клику. Текст приходит уже
// выбранным и локализованным: QML не знает ни о namерениях, ни о каталоге.
Item {
    id: root

    property string text: ""
    readonly property bool shown: text.length > 0

    implicitWidth: plate.width
    implicitHeight: plate.height + tail.height

    visible: opacity > 0
    opacity: shown ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: plate

        // Ширина по тексту, но не шире отведённого места: длинная реплика
        // переносится, а не растягивает окно.
        width: Math.min(label.implicitWidth + 22, root.parent ? root.parent.width - 12 : 240)
        height: label.implicitHeight + 16
        radius: 10
        color: "#f2fbf4"
        border.color: "#3a7d4a"
        border.width: 1

        Text {
            id: label
            anchors.centerIn: parent
            width: plate.width - 22
            text: root.text
            color: "#1d2b21"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Хвостик к питомцу. Повёрнутый квадрат вместо картинки: одна фигура
    // вместо ещё одного ассета.
    Rectangle {
        id: tail
        width: 12
        height: 12
        color: plate.color
        border.color: plate.border.color
        border.width: 1
        rotation: 45
        anchors.horizontalCenter: plate.horizontalCenter
        anchors.top: plate.bottom
        anchors.topMargin: -7
    }

    // Перекрывает шов между хвостиком и плашкой, иначе видна линия рамки.
    Rectangle {
        width: 14
        height: 3
        color: plate.color
        anchors.horizontalCenter: plate.horizontalCenter
        anchors.top: plate.bottom
        anchors.topMargin: -2
    }
}
