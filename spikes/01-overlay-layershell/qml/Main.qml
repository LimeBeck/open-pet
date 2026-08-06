import QtQuick

Window {
    id: root

    // Окно намеренно заметно больше питомца: поля вокруг силуэта — это и есть
    // та мёртвая зона, ради измерения которой затеян спайк.
    width: 300
    height: 260
    visible: false
    color: "transparent"
    title: "open-pet spike: overlay"

    Pet {
        id: pet
        x: 70
        y: 90
    }

    Hud {
        id: hud
        x: 8
        y: 8
        visible: hudVisible
    }

    // Ловит клики по всему окну, а не только по питомцу: так видно, дошло ли
    // событие туда, где его быть не должно.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                spike.cycleMode()
            else
                spike.noteClick(mouse.x, mouse.y)
        }
    }

    Component.onCompleted: {
        const bounds = pet.visualBounds
        spike.setPetRect(pet.x + bounds.x, pet.y + bounds.y, bounds.width, bounds.height)
    }
}
