pragma ComponentBehavior: Bound

import QtQuick

// Встроенный питомец M1.
//
// Нарисован примитивами QML намеренно: формат Pet Pack и его валидатор —
// этап M5, а до тех пор скелету нужен питомец, который не тянет за собой
// ни ассетов, ни лицензий. Каждое из восьми состояний §4.1 отличается
// видимо, иначе по картинке нельзя проверить, что ядро вообще работает.
//
// Силуэт намеренно с дырами — просвет между ушами и промежуток между лапами:
// на них проверяется, что input region по альфа-каналу (ADR-002) не съедает
// клики по рабочему столу.
Item {
    id: root

    // Имя эмоции из ядра: idle, happy, curious, sleepy, charging,
    // low_battery, notification, busy.
    property string emotion: "idle"
    property bool animated: true

    implicitWidth: 160
    implicitHeight: 170


    readonly property color bodyColor: {
        switch (root.emotion) {
        case "charging": return "#7fd67f"
        case "low_battery": return "#e07a5f"
        case "sleepy": return "#9aa7c7"
        case "busy": return "#b39ddb"
        case "notification": return "#ffd166"
        default: return "#f2b950"
        }
    }

    // root. обязателен: неквалифицированное обращение здесь один раз
    // вычислилось и перестало пересчитываться — уши и лапы оставались
    // цвета первого состояния, пока тело меняло цвет.
    readonly property color earColor: Qt.darker(root.bodyColor, 1.15)

    // Тень: альфа ниже порога попаданий, кликов ловить не должна.
    Rectangle {
        width: 96
        height: 18
        radius: height / 2
        color: "#22000000"
        anchors.horizontalCenter: parent.horizontalCenter
        y: 150
    }

    Item {
        id: body
        width: parent.width
        height: parent.height

        // Дыхание. При reduced motion замирает в нейтральном положении (§7).
        y: 4
        NumberAnimation on y {
            running: root.animated
            from: 0
            to: 8
            duration: root.emotion === "sleepy" ? 2600 : 1400
            easing.type: Easing.InOutSine
            loops: Animation.Infinite
            onStopped: body.y = 4
        }

        Repeater {
            model: [-28, 28]
            Rectangle {
                required property int modelData
                width: 34
                height: 34
                radius: 6
                rotation: 45
                color: root.earColor
                x: body.width / 2 - width / 2 + modelData
                y: root.emotion === "sleepy" ? 20 : 12

                Behavior on y {
                    enabled: root.animated
                    NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
                }
            }
        }

        Rectangle {
            id: head
            width: 108
            height: 96
            radius: 46
            color: root.bodyColor
            anchors.horizontalCenter: parent.horizontalCenter
            y: 26

            Behavior on color {
                enabled: root.animated
                ColorAnimation { duration: 320 }
            }

            Row {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -6
                spacing: root.emotion === "curious" ? 30 : 26

                Repeater {
                    model: 2
                    Rectangle {
                        id: eye
                        property bool blinking: false
                        // Во сне глаза закрыты, в удивлении — шире обычного.
                        readonly property bool shut: root.emotion === "sleepy" || blinking

                        width: root.emotion === "curious" ? 14 : 12
                        height: shut ? 2 : (root.emotion === "curious" ? 17 : 14)
                        radius: 6
                        color: "#2b2118"
                        y: shut ? 6 : 0

                        Timer {
                            interval: 3400
                            running: root.animated && root.emotion !== "sleepy"
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

            // Рот: улыбка в happy, ровная линия в остальных состояниях.
            Rectangle {
                width: root.emotion === "happy" ? 26 : 20
                height: root.emotion === "happy" ? 12 : 8
                radius: root.emotion === "happy" ? 6 : 4
                color: "#2b2118"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 60
            }
        }

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
                    color: root.earColor
                }
            }
        }

        // Значок состояния. Нужен не для красоты: без него состояния
        // charging и low_battery различались бы только оттенком.
        Text {
            id: badge
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: 46
            y: 18
            font.pixelSize: 26
            visible: text.length > 0
            text: {
                switch (root.emotion) {
                case "charging": return "⚡"
                case "low_battery": return "🪫"
                case "notification": return "🔔"
                case "busy": return "🎧"
                case "curious": return "❓"
                case "sleepy": return "💤"
                default: return ""
                }
            }

            SequentialAnimation on scale {
                running: root.animated && badge.visible
                loops: Animation.Infinite
                NumberAnimation { from: 0.9; to: 1.1; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.1; to: 0.9; duration: 700; easing.type: Easing.InOutSine }
            }
        }
    }
}
