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

#define OPENPET_ABI_VERSION 10

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
    // Питомца потащили курсором. Отдельно от клика: это разные обращения,
    // и общий cooldown съедал бы второе.
    OPENPET_EVENT_PET_DRAGGED = 8,
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

// Процедурное движение ([ADR-009](../../../docs/adr/0009-procedural-motion-layer.md)).
//
// Ядро отдаёт уже проверенные точки: доли по возрастанию, от 0.0 до 1.0,
// смещения в пределах лимита. Хост интерполирует между ними и не проверяет
// ничего заново.
#define OPENPET_MAX_KEYFRAMES 32

typedef enum {
    OPENPET_EASING_LINEAR = 0,
    OPENPET_EASING_IN_QUAD = 1,
    OPENPET_EASING_OUT_QUAD = 2,
    OPENPET_EASING_IN_OUT_QUAD = 3,
} OpenPetEasing;

typedef struct {
    float at;
    float x;
    float y;
    uint32_t easing;
} OpenPetKeyframe;

typedef struct {
    // 0 означает, что у анимации движения нет: слой остаётся тождественным,
    // и поведение пакетов без motion не меняется.
    uint32_t keyframe_count;
    uint32_t duration_ms;
    uint8_t loops;
    OpenPetKeyframe keyframes[OPENPET_MAX_KEYFRAMES];
} OpenPetMotion;

// Резерв под траекторию для всего активного пакета, в логических пикселях.
// Поверхность получает его один раз: растягивать её на каждое движение
// дорого, а движение случается часто.
typedef struct {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} OpenPetMotionEnvelope;


// Провайдер LLM. Секретов здесь нет: ключ добавляет хост непосредственно
// перед отправкой, до ядра он не доходит (ADR-008).
typedef enum {
    OPENPET_LLM_DISABLED = 0,
    OPENPET_LLM_OLLAMA = 1,
    OPENPET_LLM_OPENAI_COMPATIBLE = 2,
    OPENPET_LLM_VERTEX_AI = 3,
    // Тот же Gemini, что и Vertex, но с обычным ключом вместо OAuth.
    // Формат запроса и ответа совпадает; различаются адрес и заголовок.
    OPENPET_LLM_GOOGLE_AI_STUDIO = 4,
} OpenPetLlmKind;

typedef struct {
    uint32_t kind;
    const char *base_url;
    uintptr_t base_url_len;
    const char *model;
    uintptr_t model_len;
    const char *project;
    uintptr_t project_len;
    const char *region;
    uintptr_t region_len;
} OpenPetLlmConfig;

#define OPENPET_LLM_URL_SIZE 512
#define OPENPET_LLM_BODY_SIZE 2048

// Готовый запрос. Хост выполняет его как есть и не вправе добавлять в тело
// ничего сверх этого: что покидает машину, решает ядро (§US-06).
typedef struct {
    char url[OPENPET_LLM_URL_SIZE];
    char body[OPENPET_LLM_BODY_SIZE];
    uint32_t timeout_ms;
} OpenPetLlmRequest;

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

// Микродвижение в покое: питомец сам оглядывается, чтобы не выглядеть
// картинкой. Эмоция при этом не меняется — меняется только анимация.
// Реплик такое движение не порождает никогда (§4.3).
//
// Возвращает 1, если анимацию нужно сменить, 0 — если ничего не происходит.
int32_t openpet_core_fidget(OpenPetCore *core, OpenPetReaction *out_reaction);

// Порог низкого заряда в процентах.
void openpet_core_set_low_battery_threshold(OpenPetCore *core, uint8_t percent);

// Локаль реплик: тег вида "ru", "ru_RU.UTF-8", "en-GB". Всё неизвестное
// приводится к английскому — fallback обязан быть определён всегда (§7).
void openpet_core_set_locale(OpenPetCore *core, const char *tag, uintptr_t tag_len);

// Размер окна истории показанных реплик (§FR-6).
void openpet_core_set_phrase_history_limit(OpenPetCore *core, uint32_t limit);

// Забыть историю показанных реплик — часть «сбросить локальные данные» (§9).
void openpet_core_clear_phrase_history(OpenPetCore *core);

// Настройка провайдера. kind == OPENPET_LLM_DISABLED выключает LLM,
// и тогда приложение не делает сетевых запросов вовсе (§7).
void openpet_core_set_llm(OpenPetCore *core, const OpenPetLlmConfig *config);
uint8_t openpet_core_llm_enabled(const OpenPetCore *core);

// Готовит запрос для последней сформированной реакции. Возвращает 1,
// если запрос есть, 0 — если говорить не о чем или LLM выключена.
int32_t openpet_core_build_llm_request(OpenPetCore *core, OpenPetLlmRequest *out_request);

#define OPENPET_TOKEN_SIZE 2048

// Строит запрос обмена учётных данных Google ADC на токен доступа.
//
// Учётные данные проходят через ядро, но не сохраняются в нём: см. раздел
// «Уточнение» в ADR-008. Возвращает 1 при успехе, -3 если это учётные данные
// сервисного аккаунта (нужен RS256, которого здесь нет), 0 в остальных
// случаях негодного файла.
int32_t openpet_core_build_token_request(OpenPetCore *core, const char *adc, uintptr_t adc_len,
                                         OpenPetLlmRequest *out_request);

// Разбирает ответ службы аутентификации. Возвращает 1 при успехе;
// срок жизни токена в секундах кладётся в out_expires_seconds.
int32_t openpet_core_accept_token_response(OpenPetCore *core, const char *raw, uintptr_t raw_len,
                                           char *out_token, uintptr_t token_size,
                                           uint32_t *out_expires_seconds);

// Готовит запрос проверки связи (§FR-7, health_check). Тела у него нет —
// это GET, и хост различает запросы по пустому body.
int32_t openpet_core_build_health_request(OpenPetCore *core, OpenPetLlmRequest *out_request);

#define OPENPET_MODEL_LIST_SIZE 4096

// Разбирает тот же ответ и выкладывает имена моделей через перевод строки.
// Возвращает число моделей, 0 если список пуст, <0 если ответ не разобран.
//
// Отдельно от проверки связи: та отвечает «да» или «нет», а списком
// заполняется выпадающий список в настройках.
int32_t openpet_core_accept_model_list(OpenPetCore *core, const char *raw, uintptr_t raw_len,
                                       char *out_names, uintptr_t names_size);

// Разбирает ответ проверки связи. Возвращает:
//   1 — провайдер ответил и настроенная модель у него есть;
//   0 — провайдер ответил, но модели нет: это разные беды, и путать их
//       нельзя, иначе пользователь будет чинить сеть вместо опечатки;
//  <0 — ответ не разобран.
int32_t openpet_core_accept_health_response(OpenPetCore *core, const char *raw, uintptr_t raw_len);

// Разбирает и очищает ответ провайдера. Возвращает 1 при годной фразе,
// 0 при негодной — во втором случае хост показывает локальный шаблон (§FR-6).
int32_t openpet_core_accept_llm_response(OpenPetCore *core, const char *raw, uintptr_t raw_len,
                                         char *out_phrase, uintptr_t out_size);

#define OPENPET_REPORT_SIZE 2048

// Устанавливает Pet Pack из архива (§US-07).
//
// Размеры листа читаются ядром из самого PNG: хост их не сообщает, потому
// что до распаковки знать их не может, а распаковка происходит внутри.
//
// Возвращает 1 при успехе, 0 при отказе. В out_report кладётся список
// замечаний в обоих случаях: отказ без внятного списка ошибок — это
// «не получилось», а §US-07 требует объяснить, почему.
int32_t openpet_core_install_pack(OpenPetCore *core, const char *archive, uintptr_t archive_len,
                                  char *out_report, uintptr_t report_size);

// Откат к последнему рабочему пакету (§10).
void openpet_core_rollback_pack(OpenPetCore *core);

// Размер листа, ожидающего записи на диск. Ноль означает, что записывать
// нечего: активен встроенный питомец либо лист уже забрали.
//
// Ядро не пишет файлы само: у него нет ни пути к каталогу данных, ни права
// решать, куда их класть.
uintptr_t openpet_core_pending_sheet_size(OpenPetCore *core);

// Забирает байты листа; после успешного вызова ядро их не хранит.
// Возвращает число записанных байт, 0 если брать нечего, <0 при ошибке.
int32_t openpet_core_take_sheet(OpenPetCore *core, char *out, uintptr_t size);

// Идентификатор активного пакета и имя файла листа. Имя пустое, если
// активен встроенный питомец: его лист лежит в ресурсах приложения.
void openpet_core_active_pack(OpenPetCore *core, char *out_id, uintptr_t id_size,
                              char *out_sheet_file, uintptr_t sheet_size);

void openpet_core_motion_envelope(OpenPetCore *core, OpenPetMotionEnvelope *out_envelope);

// Движение для состояния активного пакета. Отсутствие движения — это
// keyframe_count == 0, а не ошибка.
void openpet_core_motion(OpenPetCore *core, const char *state, uintptr_t state_len,
                         OpenPetMotion *out_motion);

// Раскладка кадров для состояния активного Pet Pack. Неизвестное состояние
// подменяется fallbackAnimation — пустого окна не бывает (§FR-8).
void openpet_core_animation(OpenPetCore *core, const char *state, uintptr_t state_len,
                            OpenPetAnimation *out_animation);

#ifdef __cplusplus
}
#endif

#endif // OPENPET_CORE_H
