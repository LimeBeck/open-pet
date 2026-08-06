import QtQuick

// Заведомо не прямоугольный силуэт с дырами: просвет между ушами, вырезы
// по бокам от лап и полупрозрачная тень. Именно эти места отвечают на вопрос
// ADR-002 — доходит ли клик по прозрачному пикселю до окна снизу.
Item {
    id: pet

    property bool animating: true

    // Границы видимой части в координатах этого элемента. Нужны варианту A
    // (статический прямоугольник) — считаем один раз здесь, а не на глаз.
    readonly property rect visualBounds: Qt.rect(20, 4, 120, 146)

    implicitWidth: 160
    implicitHeight: 160

    // Тень: альфа ниже порога попаданий, кликов ловить не должна.
    Rectangle {
        id: shadow
        width: 96
        height: 18
        radius: height / 2
        color: "#22000000"
        anchors.horizontalCenter: parent.horizontalCenter
        y: 148
    }

    Item {
        id: body
        width: parent.width
        height: parent.height

        // Дыхание: смещение по вертикали, ради которого силуэт вообще меняется
        // между кадрами — иначе вариант C нечего было бы мерить.
        y: pet.animating ? 0 : 4
        NumberAnimation on y {
            running: pet.animating
            from: 0
            to: 8
            duration: 1400
            easing.type: Easing.InOutSine
            loops: Animation.Infinite
            onStopped: body.y = 4
        }

        // Уши: между ними остаётся прозрачный клин.
        Repeater {
            model: [-28, 28]
            Rectangle {
                required property int modelData
                width: 34
                height: 34
                radius: 6
                rotation: 45
                color: "#e8a33d"
                x: body.width / 2 - width / 2 + modelData
                y: 12
            }
        }

        Rectangle {
            id: head
            width: 108
            height: 96
            radius: 46
            color: "#f2b950"
            anchors.horizontalCenter: parent.horizontalCenter
            y: 26

            Row {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -6
                spacing: 26

                Repeater {
                    model: 2
                    Rectangle {
                        id: eye
                        property bool blinking: false

                        width: 12
                        height: blinking ? 2 : 14
                        radius: 6
                        color: "#2b2118"
                        y: blinking ? 6 : 0

                        Timer {
                            interval: 3400
                            running: pet.animating
                            repeat: true
                            onTriggered: blink.start()
                        }
                        SequentialAnimation {
                            id: blink
                            PropertyAction { target: eye; property: "blinking"; value: true }
                            PauseAnimation { duration: 110 }
                            PropertyAction { target: eye; property: "blinking"; value: false }
                        }
                    }
                }
            }

            Rectangle {
                width: 22
                height: 10
                radius: 5
                color: "#2b2118"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 60
            }
        }

        // Лапы: между ними прозрачный промежуток — ещё одна дыра в силуэте.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 22
            y: 116

            Repeater {
                model: 2
                Rectangle {
                    width: 30
                    height: 26
                    radius: 12
                    color: "#e8a33d"
                }
            }
        }
    }
}
