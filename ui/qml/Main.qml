import QtQuick

Window {
    id: root

    // Над питомцем оставлено место под пузырь с репликой. Поля вокруг
    // остаются прозрачными и кликов не ловят — за это отвечает input region
    // (ADR-002), который пересчитывается и при появлении пузыря.
    readonly property int bubbleArea: 76

    width: Math.round(petView.implicitWidth * petModel.scale)
    height: Math.round((petView.implicitHeight + bubbleArea) * petModel.scale)
    visible: false
    color: "transparent"
    title: "open-pet"

    Item {
        id: stage
        anchors.fill: parent
        scale: petModel.scale
        transformOrigin: Item.Center

        Pet {
            id: petView
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // Пауза останавливает реакции, но не замораживает питомца:
            // он остаётся живым, просто перестаёт отзываться (§FR-2).
            emotion: petModel.emotionName
            animated: !petModel.reducedMotion
        }

        Bubble {
            id: bubble
            text: petModel.phrase
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: petView.top
            // Хвостик заходит на макушку питомца, иначе пузырь висит в воздухе.
            anchors.bottomMargin: -18
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        // Клик по пузырю закрывает реплику, клик по питомцу — просит новую
        // (§FR-6, §FR-2). Правый клик и перетаскивание появятся вместе
        // с контекстным меню окна; пока меню живёт в трее.
        onClicked: (mouse) => {
            const inBubble = bubble.shown
                && mouse.y < stage.height - petView.implicitHeight * petModel.scale
            if (inBubble)
                petModel.dismissPhrase()
            else
                petModel.handleClick()
        }
    }
}
