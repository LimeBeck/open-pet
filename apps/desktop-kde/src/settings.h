#pragma once

#include "overlaysurface.h"

#include <QMargins>
#include <QString>

// Настройки, переживающие перезапуск (§US-02).
//
// Сохраняется только то, что перечислено в §9: параметры UI и разрешённых
// capabilities. Ни истории событий, ни списка приложений, ни чего-либо
// с пользовательским содержимым здесь нет и быть не может.
struct Settings {
    OverlaySurface::Corner corner = OverlaySurface::Corner::BottomRight;
    int marginRight = 8;
    int marginBottom = 8;
    qreal scale = 1.0;
    bool paused = false;
    bool reducedMotion = false;

    static Settings load();
    void save() const;
};
