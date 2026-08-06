// C ABI ядра. Заголовок написан руками намеренно: он и есть контракт,
// а не отражение внутреннего устройства Rust-кода. cbindgen сгенерировал бы
// его из lib.rs, но тогда изменение приватного типа в ядре молча меняло бы
// границу — а её изменения должны быть заметны на ревью.
//
// Правило границы: только POD-типы, никаких Qt-типов, никакого владения
// памятью, пересекающего границу.

#ifndef OPENPET_CORE_H
#define OPENPET_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Версия контракта. Хост обязан сверить её при загрузке: расхождение означает,
// что ядро и хост собраны из разных версий и работать вместе не должны.
#define OPENPET_ABI_VERSION 2

// Длина ключа cooldown с завершающим нулём. Фиксированный буфер вместо
// указателя снимает вопрос владения строкой на границе.
#define OPENPET_COOLDOWN_KEY_SIZE 32

typedef enum {
    OPENPET_EVENT_ACTIVITY_RESUMED = 0,
    OPENPET_EVENT_IDLE_THRESHOLD_REACHED = 1,
    OPENPET_EVENT_POWER_CHANGED = 2,
    OPENPET_EVENT_ACTIVE_APP_CHANGED = 3,
    OPENPET_EVENT_PET_CLICKED = 4,
    OPENPET_EVENT_NOTIFICATION_OCCURRED = 5,
} OpenPetEventKind;

typedef enum {
    OPENPET_POWER_UNKNOWN = 0,
    OPENPET_POWER_CHARGING = 1,
    OPENPET_POWER_DISCHARGING = 2,
    OPENPET_POWER_FULL = 3,
} OpenPetPowerState;

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

// Событие рабочего стола. Плоская структура вместо размеченного объединения:
// поля, не относящиеся к текущему kind, игнорируются. Это дороже по памяти
// (несколько десятков байт на событие) и заметно дешевле по сложности границы.
typedef struct {
    uint32_t kind;

    uint32_t idle_seconds;

    uint8_t on_battery;
    uint8_t battery_percent_valid;
    uint8_t battery_percent;
    uint32_t power_state;

    // Строка живёт у вызывающей стороны и валидна только на время вызова.
    // Ядро обязано скопировать то, что ему нужно, и не хранить указатель.
    const char *app_id;
    uintptr_t app_id_len;

    // Только категория, никогда не текст и не отправитель (§4.2).
    const char *category;
    uintptr_t category_len;
} OpenPetEvent;

typedef struct {
    uint32_t emotion;
    uint32_t animation;
    uint32_t phrase_intent;
    uint8_t phrase_intent_valid;
    uint8_t priority;
    uint32_t ttl_ms;
    char cooldown_key[OPENPET_COOLDOWN_KEY_SIZE];
} OpenPetReaction;

typedef struct OpenPetCore OpenPetCore;

// Вызывается из потока ядра, а не из потока UI. Хост обязан перекинуть
// реакцию в свой event loop сам.
typedef void (*OpenPetReactionCallback)(const OpenPetReaction *reaction, void *user_data);

uint32_t openpet_abi_version(void);

OpenPetCore *openpet_core_new(void);
void openpet_core_free(OpenPetCore *core);

void openpet_core_set_callback(OpenPetCore *core, OpenPetReactionCallback callback, void *user_data);

// Возвращает 1, если реакция сформирована, 0 — если событие подавлено
// (cooldown или низкий приоритет), отрицательное значение — ошибка.
int32_t openpet_core_push_event(OpenPetCore *core, const OpenPetEvent *event, OpenPetReaction *out_reaction);

// Запускает фоновый поток ядра, который сам присылает реакции через callback.
void openpet_core_start_ticker(OpenPetCore *core, uint32_t interval_ms);

// Существует только ради проверки: паника в Rust не должна ронять хост.
// Возвращает отрицательное значение, если паника была перехвачена.
int32_t openpet_core_simulate_panic(OpenPetCore *core);

#ifdef __cplusplus
}
#endif

#endif // OPENPET_CORE_H
