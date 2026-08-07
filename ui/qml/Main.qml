import QtQuick

Window {
    id: root

    // Окно чуть больше ячейки спрайта: запас нужен под будущий пузырь реплики
    // (M2). Поля вокруг питомца остаются прозрачными и кликов не ловят —
    // за это отвечает input region (ADR-002).
    width: Math.round(petView.implicitWidth * petModel.scale)
    height: Math.round(petView.implicitHeight * petModel.scale)
    visible: false
    color: "transparent"
    title: "open-pet"

    Item {
        anchors.centerIn: parent
        width: petView.implicitWidth
        height: petView.implicitHeight
        scale: petModel.scale

        Pet {
            id: petView
            emotion: petModel.emotionName
            // Пауза останавливает реакции, но не замораживает питомца:
            // он остаётся живым, просто перестаёт отзываться (§FR-2).
            animated: !petModel.reducedMotion
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        // Левый клик — дружелюбная реакция с cooldown (§FR-2). Правый клик
        // и перетаскивание появятся вместе с контекстным меню окна;
        // пока меню живёт в трее.
        onClicked: petModel.handleClick()
    }
}
