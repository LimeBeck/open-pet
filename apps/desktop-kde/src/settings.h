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
    // Порог простоя в секундах (§US-03: «настраиваемый период простоя»).
    int idleSeconds = 300;

    // Переключатель на каждый класс событий (§9). Выключенный источник
    // не подписывается на события вовсе, а не фильтрует их после получения.
    bool sourceIdle = true;
    bool sourcePower = true;
    bool sourceSession = true;
    bool sourceMedia = true;
    bool sourceNotification = true;
    bool sourceActiveApp = true;

    // LLM. Ключа здесь нет и быть не может: он живёт в KWallet (§FR-7).
    // 0 — выключено, 1 — Ollama, 2 — OpenAI-совместимый, 3 — Vertex AI.
    int llmKind = 0;
    QString llmBaseUrl;
    QString llmModel;
    QString llmProject;
    QString llmRegion;
    int llmTimeoutMs = 2500;

    // Сбросить локальные данные (§9): настройки и история. Импортированные
    // Pet Pack не трогаются без отдельного подтверждения.
    static void resetLocalData();

    static Settings load();
    void save() const;
};
