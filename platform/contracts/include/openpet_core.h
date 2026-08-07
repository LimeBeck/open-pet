// Контракт границы host↔core.
//
// Заголовок написан руками намеренно ([ADR-001](../../../docs/adr/0001-rust-qt-bridge.md)):
// он и есть контракт, а не отражение внутреннего устройства Rust-кода.
// Изменения границы должны быть заметны на ревью, а не проступать из генератора.
//
// Правила, зафиксированные ADR-001:
//   1. Через границу ходят только POD-типы. Никаких Qt-типов.
//   2. Событие — плоская структура; поля, не относящиеся к kind, игнорируются.
//      Хост обязан обнулять структуру перед заполнением.
//   3. Строки наружу — фиксированный буфер, внутрь — указатель с длиной,
//      валидный только на время вызова.
//   4. Паника не пересекает границу никогда.
//   5. Любое изменение раскладки поднимает OPENPET_ABI_VERSION.

#ifndef OPENPET_CORE_H
#define OPENPET_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENPET_ABI_VERSION 3

// Длина ключа cooldown с завершающим нулём.
#define OPENPET_COOLDOWN_KEY_SIZE 32

// Длина реплики с завершающим нулём. Фиксированный буфер вместо указателя
// снимает вопрос владения строкой на границе (ADR-001, правило 3).
// Короткая реплика — продуктовое требование §FR-6, а не экономия байт.
#define OPENPET_PHRASE_SIZE 192

// Нормализованная модель событий (§FR-4).
typedef enum {
    OPENPET_EVENT_ACTIVITY_RESUMED = 0,
    OPENPET_EVENT_IDLE_THRESHOLD_REACHED = 1,
    OPENPET_EVENT_POWER_CHANGED = 2,
    OPENPET_EVENT_SESSION_CHANGED = 3,
    OPENPET_EVENT_ACTIVE_APP_CHANGED = 4,
    OPENPET_EVENT_NOTIFICATION_OCCURRED = 5,
    OPENPET_EVENT_MEDIA_CHANGED = 6,
    OPENPET_EVENT_PET_CLICKED = 7,
} OpenPetEventKind;

typedef enum {
    OPENPET_POWER_UNKNOWN = 0,
    OPENPET_POWER_CHARGING = 1,
    OPENPET_POWER_DISCHARGING = 2,
    OPENPET_POWER_FULL = 3,
} OpenPetPowerState;

typedef enum {
    OPENPET_SESSION_ACTIVE = 0,
    OPENPET_SESSION_LOCKED = 1,
    OPENPET_SESSION_SLEEPING = 2,
    OPENPET_SESSION_RESUMED = 3,
} OpenPetSessionState;

typedef enum {
    OPENPET_MEDIA_STOPPED = 0,
    OPENPET_MEDIA_PLAYING = 1,
    OPENPET_MEDIA_PAUSED = 2,
} OpenPetMediaState;

// Восемь состояний из §4.1.
typedef enum {
    OPENPET_EMOTION_IDLE = 0,
    OPENPET_EMOTION_HAPPY = 1,
    OPENPET_EMOTION_CURIOUS = 2,
    OPENPET_EMOTION_SLEEPY = 3,
    OPENPET_EMOTION_CHARGING = 4,
    OPENPET_EMOTION_LOW_BATTERY = 5,
    OPENPET_EMOTION_NOTIFICATION = 6,
    OPENPET_EMOTION_BUSY = 7,
} OpenPetEmotion;

// Состояние здоровья источника событий (§FR-4).
typedef enum {
    OPENPET_CAPABILITY_AVAILABLE = 0,
    OPENPET_CAPABILITY_PERMISSION_REQUIRED = 1,
    OPENPET_CAPABILITY_UNSUPPORTED = 2,
    OPENPET_CAPABILITY_DEGRADED = 3,
} OpenPetCapabilityState;

typedef struct {
    uint32_t kind;

    uint32_t idle_seconds;

    uint8_t on_battery;
    uint8_t battery_percent_valid;
    uint8_t battery_percent;
    uint32_t power_state;

    uint32_t session_state;
    uint32_t media_state;

    // Нормализованный идентификатор приложения. Никогда не заголовок окна
    // и не содержимое документа (§4.2).
    const char *app_id;
    uintptr_t app_id_len;

    // Только категория уведомления. Никогда не текст и не отправитель (§4.2).
    const char *category;
    uintptr_t category_len;
} OpenPetEvent;

typedef struct {
    uint32_t emotion;
    uint32_t animation;
    uint8_t priority;
    uint32_t ttl_ms;
    char cooldown_key[OPENPET_COOLDOWN_KEY_SIZE];

    // Текст реплики в UTF-8 на текущей локали, уже выбранный ядром
    // с учётом истории показов (§FR-6). Пустой, если питомец молчит:
    // не каждая смена позы заслуживает слов.
    uint8_t has_phrase;
    char phrase[OPENPET_PHRASE_SIZE];
} OpenPetReaction;

// Кадры анимации в координатах спрайтового листа.
//
// UI спрашивает раскладку у ядра и не хранит собственную таблицу: иначе
// второй способ отрисовки пришлось бы вживлять в QML (ADR-005).
typedef struct {
    uint32_t row;
    uint32_t start_column;
    uint32_t frames;
    uint32_t frame_duration_ms;
    uint32_t cell_width;
    uint32_t cell_height;
} OpenPetAnimation;

typedef struct OpenPetCore OpenPetCore;

// Вызывается из потока ядра, а не из потока UI. Перенос в event loop —
// обязанность хоста.
typedef void (*OpenPetReactionCallback)(const OpenPetReaction *reaction, void *user_data);

// Сообщения диагностики. Пользовательского содержимого в них не бывает (§9).
typedef void (*OpenPetLogCallback)(int32_t level, const char *message, void *user_data);

uint32_t openpet_abi_version(void);

OpenPetCore *openpet_core_new(void);
void openpet_core_free(OpenPetCore *core);

void openpet_core_set_reaction_callback(OpenPetCore *core, OpenPetReactionCallback callback, void *user_data);
void openpet_core_set_log_callback(OpenPetCore *core, OpenPetLogCallback callback, void *user_data);

// Возвращает 1, если реакция сформирована, 0 — если событие подавлено
// (пауза, cooldown или более приоритетное активное состояние),
// отрицательное значение — ошибка.
int32_t openpet_core_push_event(OpenPetCore *core, const OpenPetEvent *event, OpenPetReaction *out_reaction);

// Пауза реакций (§FR-2). Источники событий при этом продолжают работать:
// подавление происходит в ядре, чтобы правило было одно и то же для всех.
void openpet_core_set_paused(OpenPetCore *core, uint8_t paused);
uint8_t openpet_core_is_paused(const OpenPetCore *core);

// Текущая эмоция — для восстановления состояния UI после показа окна.
uint32_t openpet_core_current_emotion(const OpenPetCore *core);

// Возврат в покой по истечении ttl текущего состояния. Хост опрашивает
// периодически; возвращает 1, если состояние сменилось, и записывает
// новую эмоцию в out_emotion.
int32_t openpet_core_settle(OpenPetCore *core, uint32_t *out_emotion);

// Порог низкого заряда в процентах.
void openpet_core_set_low_battery_threshold(OpenPetCore *core, uint8_t percent);

// Локаль реплик: тег вида "ru", "ru_RU.UTF-8", "en-GB". Всё неизвестное
// приводится к английскому — fallback обязан быть определён всегда (§7).
void openpet_core_set_locale(OpenPetCore *core, const char *tag, uintptr_t tag_len);

// Размер окна истории показанных реплик (§FR-6).
void openpet_core_set_phrase_history_limit(OpenPetCore *core, uint32_t limit);

// Забыть историю показанных реплик — часть «сбросить локальные данные» (§9).
void openpet_core_clear_phrase_history(OpenPetCore *core);

// Раскладка кадров для состояния активного Pet Pack. Неизвестное состояние
// подменяется fallbackAnimation — пустого окна не бывает (§FR-8).
void openpet_core_animation(OpenPetCore *core, const char *state, uintptr_t state_len,
                            OpenPetAnimation *out_animation);

#ifdef __cplusplus
}
#endif

#endif // OPENPET_CORE_H
