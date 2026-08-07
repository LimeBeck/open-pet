// KWin-скрипт open-pet: сообщает приложению только идентификатор активного окна.
//
// Обычный клиент Wayland не может наблюдать чужие окна — это позиция модели
// безопасности протокола, а не пробел в реализации. Поэтому наблюдатель живёт
// внутри композитора, а наружу отдаёт минимум.
//
// Что уходит: resourceClass — идентификатор приложения вида "org.kde.konsole".
// Что НЕ уходит и не должно быть сюда добавлено: caption (заголовок окна),
// геометрия, идентификатор окна, pid. Заголовок содержит имя открытого
// документа, а это прямо запрещено §4.2 спецификации.

function reportActiveWindow(window) {
    if (!window) {
        return;
    }

    // Служебные поверхности самого рабочего стола активацией не считаются:
    // иначе питомец реагировал бы на собственный пузырь и на панель.
    if (window.desktopWindow || window.dock || window.popupWindow ||
        window.notification || window.criticalNotification || window.tooltip ||
        window.onScreenDisplay) {
        return;
    }

    var appId = window.resourceClass;
    if (!appId) {
        return;
    }

    // Реакция на самого себя выглядела бы как разговор с зеркалом.
    if (appId === "open-pet") {
        return;
    }

    callDBus("org.openpet.DesktopPet",
             "/org/openpet/DesktopPet",
             "org.openpet.DesktopPet",
             "SetActiveApp",
             appId);
}

workspace.windowActivated.connect(reportActiveWindow);
