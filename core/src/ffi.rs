//! Слой FFI: перевод между C-структурами из `platform/contracts` и доменом.
//!
//! Здесь нет ни правил поведения, ни знания о Qt — только перевод и защита
//! хоста от паники. При смене способа связывания выбрасывается этот файл
//! и C++-обёртка; домен не затрагивается (ADR-001).

use crate::behavior::{Reaction, StateMachine, Suppressed};
use crate::emotion::Emotion;
use crate::event::{DesktopEvent, MediaState, PowerState, SessionState};
use crate::llm::{self, PhraseRequest, Provider};
use crate::petpack::{ArchiveLimits, Limits, PackStore, SheetSource};
use crate::phrase::{Locale, PhraseBook};

use std::os::raw::{c_char, c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

pub const ABI_VERSION: u32 = 8;
const COOLDOWN_KEY_SIZE: usize = 32;
const PHRASE_SIZE: usize = 192;

const EVENT_ACTIVITY_RESUMED: u32 = 0;
const EVENT_IDLE_THRESHOLD_REACHED: u32 = 1;
const EVENT_POWER_CHANGED: u32 = 2;
const EVENT_SESSION_CHANGED: u32 = 3;
const EVENT_ACTIVE_APP_CHANGED: u32 = 4;
const EVENT_NOTIFICATION_OCCURRED: u32 = 5;
const EVENT_MEDIA_CHANGED: u32 = 6;
const EVENT_PET_CLICKED: u32 = 7;

const LOG_WARNING: i32 = 1;

#[repr(C)]
pub struct FfiEvent {
    kind: u32,
    idle_seconds: u32,
    on_battery: u8,
    battery_percent_valid: u8,
    battery_percent: u8,
    power_state: u32,
    session_state: u32,
    media_state: u32,
    app_id: *const c_char,
    app_id_len: usize,
    category: *const c_char,
    category_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiReaction {
    emotion: u32,
    animation: u32,
    priority: u8,
    ttl_ms: u32,
    cooldown_key: [c_char; COOLDOWN_KEY_SIZE],
    has_phrase: u8,
    phrase: [c_char; PHRASE_SIZE],
}

pub type ReactionCallback = extern "C" fn(*const FfiReaction, *mut c_void);
pub type LogCallback = extern "C" fn(i32, *const c_char, *mut c_void);

/// Указатель на данные хоста. `*mut c_void` не является `Send`/`Sync`,
/// поэтому обёртка нужна, чтобы отдать его в фоновые потоки ядра.
///
/// Обязательства сторон: хост держит объект живым не меньше, чем ядро,
/// и снимает callback до разрушения; ядро указатель не разыменовывает,
/// а только передаёт обратно.
struct Callbacks {
    reaction: Option<(ReactionCallback, *mut c_void)>,
    log: Option<(LogCallback, *mut c_void)>,
}

unsafe impl Send for Callbacks {}
unsafe impl Sync for Callbacks {}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FfiAnimation {
    row: u32,
    start_column: u32,
    frames: u32,
    frame_duration_ms: u32,
    cell_width: u32,
    cell_height: u32,
}

const LLM_URL_SIZE: usize = 512;
const LLM_BODY_SIZE: usize = 2048;

#[repr(C)]
pub struct FfiLlmConfig {
    kind: u32,
    base_url: *const c_char,
    base_url_len: usize,
    model: *const c_char,
    model_len: usize,
    project: *const c_char,
    project_len: usize,
    region: *const c_char,
    region_len: usize,
}

#[repr(C)]
pub struct FfiLlmRequest {
    url: [c_char; LLM_URL_SIZE],
    body: [c_char; LLM_BODY_SIZE],
    timeout_ms: u32,
}

pub struct Core {
    machine: Mutex<StateMachine>,
    phrases: Mutex<PhraseBook>,
    packs: Mutex<PackStore>,
    callbacks: Mutex<Callbacks>,
    /// Настроенный провайдер. `None` означает, что сеть не используется
    /// вовсе — не «используется с ошибкой», а не используется (§7).
    provider: Mutex<Option<Provider>>,
    /// Повод последней реакции. Хранится здесь, чтобы намерение не пришлось
    /// тащить через границу: хост запрашивает план сразу после реакции.
    last_request: Mutex<Option<PhraseRequest>>,
}

impl Core {
    fn new() -> Self {
        Self {
            machine: Mutex::new(StateMachine::new()),
            phrases: Mutex::new(PhraseBook::default()),
            packs: Mutex::new(PackStore::new()),
            provider: Mutex::new(None),
            last_request: Mutex::new(None),
            callbacks: Mutex::new(Callbacks {
                reaction: None,
                log: None,
            }),
        }
    }

    /// Сообщения диагностики формирует само ядро, поэтому пользовательского
    /// содержимого в них не бывает по построению (§9).
    fn log(&self, level: i32, message: &str) {
        let Ok(callbacks) = self.callbacks.lock() else {
            return;
        };
        let Some((callback, user_data)) = callbacks.log else {
            return;
        };

        // Сообщения короткие и известны заранее, но защита от внутреннего
        // нуля всё равно нужна: без неё C-сторона прочитает обрезанную строку.
        let mut bytes = message.replace('\0', "?").into_bytes();
        bytes.push(0);
        callback(level, bytes.as_ptr() as *const c_char, user_data);
    }
}

const fn emotion_code(emotion: Emotion) -> u32 {
    match emotion {
        Emotion::Idle => 0,
        Emotion::Happy => 1,
        Emotion::Curious => 2,
        Emotion::Sleepy => 3,
        Emotion::Charging => 4,
        Emotion::LowBattery => 5,
        Emotion::Notification => 6,
        Emotion::Busy => 7,
    }
}

/// Копирует UTF-8 в фиксированный буфер, обрезая **по границе символа**.
/// Обрыв посередине многобайтового символа дал бы невалидную строку
/// на стороне C, а реплики на русском — сплошь многобайтовые.
fn fill_utf8(buffer: &mut [c_char], text: &str) {
    let limit = buffer.len() - 1;
    let mut end = text.len().min(limit);
    while end > 0 && !text.is_char_boundary(end) {
        end -= 1;
    }
    for (slot, byte) in buffer.iter_mut().zip(&text.as_bytes()[..end]) {
        *slot = *byte as c_char;
    }
}

fn to_ffi(reaction: &Reaction, phrase: Option<&str>) -> FfiReaction {
    let mut cooldown_key = [0 as c_char; COOLDOWN_KEY_SIZE];
    if let Some(key) = &reaction.cooldown_key {
        // Обрезаем молча: ключ формирует само ядро, это не пользовательские
        // данные и не строка переменной длины извне.
        for (slot, byte) in cooldown_key
            .iter_mut()
            .take(COOLDOWN_KEY_SIZE - 1)
            .zip(key.as_bytes())
        {
            *slot = *byte as c_char;
        }
    }

    let mut phrase_buffer = [0 as c_char; PHRASE_SIZE];
    if let Some(text) = phrase {
        fill_utf8(&mut phrase_buffer, text);
    }

    FfiReaction {
        emotion: emotion_code(reaction.emotion),
        animation: emotion_code(reaction.animation),
        priority: reaction.priority,
        ttl_ms: reaction.ttl_ms,
        cooldown_key,
        has_phrase: u8::from(phrase.is_some()),
        phrase: phrase_buffer,
    }
}

/// # Safety
/// Указатели строк должны быть валидны на время вызова и указывать
/// на буфер заявленной длины.
unsafe fn read_string(pointer: *const c_char, len: usize) -> Option<String> {
    if pointer.is_null() || len == 0 {
        return None;
    }

    let bytes = std::slice::from_raw_parts(pointer as *const u8, len);
    // Невалидный UTF-8 не роняет ядро: строка приходит извне, и доверять
    // её кодировке нельзя.
    std::str::from_utf8(bytes).ok().map(str::to_string)
}

const fn power_state_from_code(code: u32) -> PowerState {
    match code {
        1 => PowerState::Charging,
        2 => PowerState::Discharging,
        3 => PowerState::Full,
        _ => PowerState::Unknown,
    }
}

const fn session_state_from_code(code: u32) -> Option<SessionState> {
    match code {
        0 => Some(SessionState::Active),
        1 => Some(SessionState::Locked),
        2 => Some(SessionState::Sleeping),
        3 => Some(SessionState::Resumed),
        _ => None,
    }
}

const fn media_state_from_code(code: u32) -> Option<MediaState> {
    match code {
        0 => Some(MediaState::Stopped),
        1 => Some(MediaState::Playing),
        2 => Some(MediaState::Paused),
        _ => None,
    }
}

/// # Safety
/// `event` должен указывать на валидную структуру со строками,
/// живыми на время вызова.
unsafe fn to_domain(event: &FfiEvent) -> Option<DesktopEvent> {
    let domain = match event.kind {
        EVENT_ACTIVITY_RESUMED => DesktopEvent::ActivityResumed,

        EVENT_IDLE_THRESHOLD_REACHED => DesktopEvent::IdleThresholdReached {
            seconds: event.idle_seconds,
        },

        EVENT_POWER_CHANGED => DesktopEvent::PowerChanged {
            on_battery: event.on_battery != 0,
            percent: (event.battery_percent_valid != 0).then_some(event.battery_percent),
            state: power_state_from_code(event.power_state),
        },

        EVENT_SESSION_CHANGED => DesktopEvent::SessionChanged {
            state: session_state_from_code(event.session_state)?,
        },

        EVENT_ACTIVE_APP_CHANGED => DesktopEvent::ActiveAppChanged {
            app_id: read_string(event.app_id, event.app_id_len),
        },

        EVENT_NOTIFICATION_OCCURRED => DesktopEvent::NotificationOccurred {
            category: read_string(event.category, event.category_len),
        },

        EVENT_MEDIA_CHANGED => DesktopEvent::MediaChanged {
            state: media_state_from_code(event.media_state)?,
        },

        EVENT_PET_CLICKED => DesktopEvent::PetClicked,

        _ => return None,
    };

    Some(domain)
}

#[no_mangle]
pub extern "C" fn openpet_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub extern "C" fn openpet_core_new() -> *mut Core {
    match catch_unwind(|| Box::into_raw(Box::new(Core::new()))) {
        Ok(pointer) => pointer,
        Err(_) => std::ptr::null_mut(),
    }
}

/// # Safety
/// `core` должен быть указателем из [`openpet_core_new`] и не должен
/// использоваться после этого вызова.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_free(core: *mut Core) {
    if core.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(core))));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро, а `user_data` — жить
/// не меньше, чем установленный callback.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_reaction_callback(
    core: *mut Core,
    callback: Option<ReactionCallback>,
    user_data: *mut c_void,
) {
    let Some(core) = core.as_ref() else { return };
    let Ok(mut callbacks) = core.callbacks.lock() else {
        return;
    };
    callbacks.reaction = callback.map(|callback| (callback, user_data));
}

/// # Safety
/// См. [`openpet_core_set_reaction_callback`].
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_log_callback(
    core: *mut Core,
    callback: Option<LogCallback>,
    user_data: *mut c_void,
) {
    let Some(core) = core.as_ref() else { return };
    let Ok(mut callbacks) = core.callbacks.lock() else {
        return;
    };
    callbacks.log = callback.map(|callback| (callback, user_data));
}

/// # Safety
/// `core`, `event` и `out_reaction` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_push_event(
    core: *mut Core,
    event: *const FfiEvent,
    out_reaction: *mut FfiReaction,
) -> c_int {
    let (Some(core), Some(event)) = (core.as_ref(), event.as_ref()) else {
        return -1;
    };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let Some(domain) = to_domain(event) else {
            return Err(None);
        };
        let mut machine = core.machine.lock().map_err(|_| None)?;
        machine.handle(domain).map_err(Some)
    }));

    match outcome {
        Ok(Ok(reaction)) => {
            // Текст подбирается здесь, а не в rule engine: правило знает
            // намерение, каталог — формулировку (§FR-6).
            if let (Some(intent), Ok(mut slot)) = (reaction.phrase_intent, core.last_request.lock())
            {
                let locale = core
                    .phrases
                    .lock()
                    .map(|book| book.locale())
                    .unwrap_or(Locale::En);

                *slot = Some(PhraseRequest {
                    intent,
                    emotion: reaction.emotion,
                    locale,
                });
            }

            let phrase = reaction.phrase_intent.and_then(|intent| {
                core.phrases
                    .lock()
                    .ok()
                    .and_then(|mut book| book.pick(intent))
            });

            if let Some(slot) = out_reaction.as_mut() {
                *slot = to_ffi(&reaction, phrase.as_ref().map(|p| p.text));
            }
            1
        }
        Ok(Err(Some(Suppressed::Paused))) => 0,
        Ok(Err(Some(_))) => 0,
        Ok(Err(None)) => {
            core.log(LOG_WARNING, "неизвестный вид события отклонён");
            -1
        }
        // Паника не должна пересекать границу FFI: за ней начинается C++,
        // где разворачивание стека — неопределённое поведение (ADR-001).
        Err(_) => {
            core.log(LOG_WARNING, "паника в ядре перехвачена на границе");
            -2
        }
    }
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_paused(core: *mut Core, paused: u8) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut machine) = core.machine.lock() {
            machine.set_paused(paused != 0);
        }
    }));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_is_paused(core: *const Core) -> u8 {
    let Some(core) = core.as_ref() else { return 0 };
    core.machine
        .lock()
        .map(|machine| u8::from(machine.is_paused()))
        .unwrap_or(0)
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_current_emotion(core: *const Core) -> u32 {
    let Some(core) = core.as_ref() else { return 0 };
    core.machine
        .lock()
        .map(|machine| emotion_code(machine.current_emotion()))
        .unwrap_or(0)
}

/// # Safety
/// `core` должен быть валидным указателем на ядро, `out_emotion` — валидным
/// или нулевым.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_settle(core: *mut Core, out_emotion: *mut u32) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let mut machine = core.machine.lock().ok()?;
        machine.settle_at(std::time::Instant::now())
    }));

    match outcome {
        Ok(Some(emotion)) => {
            if let Some(slot) = out_emotion.as_mut() {
                *slot = emotion_code(emotion);
            }
            1
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core` и `out_reaction` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_fidget(
    core: *mut Core,
    out_reaction: *mut FfiReaction,
) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let mut machine = core.machine.lock().ok()?;
        machine.fidget_at(std::time::Instant::now())
    }));

    match outcome {
        Ok(Some(reaction)) => {
            if let Some(slot) = out_reaction.as_mut() {
                // Реплики у микродвижения нет и быть не может.
                *slot = to_ffi(&reaction, None);
            }
            1
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_low_battery_threshold(core: *mut Core, percent: u8) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut machine) = core.machine.lock() {
            machine.set_low_battery_threshold(percent);
        }
    }));
}

/// # Safety
/// `core` должен быть валидным указателем, `tag` — строкой длиной `tag_len`,
/// живой на время вызова.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_locale(
    core: *mut Core,
    tag: *const c_char,
    tag_len: usize,
) {
    let Some(core) = core.as_ref() else { return };

    let locale = read_string(tag, tag_len)
        .map(|tag| Locale::parse(&tag))
        .unwrap_or(Locale::En);

    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut book) = core.phrases.lock() {
            book.set_locale(locale);
        }
    }));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_phrase_history_limit(core: *mut Core, limit: u32) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut book) = core.phrases.lock() {
            book.set_history_limit(limit as usize);
        }
    }));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_clear_phrase_history(core: *mut Core) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut book) = core.phrases.lock() {
            book.clear_history();
        }
    }));
}

/// # Safety
/// `core` и `out_animation` должны быть валидными указателями, `state` —
/// строкой длиной `state_len`, живой на время вызова.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_animation(
    core: *mut Core,
    state: *const c_char,
    state_len: usize,
    out_animation: *mut FfiAnimation,
) {
    let (Some(core), Some(slot)) = (core.as_ref(), out_animation.as_mut()) else {
        return;
    };

    let name = read_string(state, state_len).unwrap_or_default();

    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Ok(packs) = core.packs.lock() else { return };
        let pack = packs.active();
        let frames = pack.animation(&name);
        let (cell_width, cell_height) = pack.grid_cell();

        *slot = FfiAnimation {
            row: frames.row,
            start_column: frames.start_column,
            frames: frames.frames,
            frame_duration_ms: frames.frame_duration_ms,
            cell_width,
            cell_height,
        };
    }));
}

fn provider_from_config(config: &FfiLlmConfig) -> Option<Provider> {
    // # Safety: строки конфигурации живут на время вызова — это записано
    // в контракте, и хост обязан это соблюдать.
    let read = |ptr, len| unsafe { read_string(ptr, len) }.unwrap_or_default();

    let base_url = read(config.base_url, config.base_url_len);
    let model = read(config.model, config.model_len);
    let project = read(config.project, config.project_len);
    let region = read(config.region, config.region_len);

    match config.kind {
        1 => Some(Provider::Ollama { base_url, model }),
        2 => Some(Provider::OpenAiCompatible { base_url, model }),
        3 => Some(Provider::VertexAi {
            project,
            region,
            model,
        }),
        4 => Some(Provider::GoogleAiStudio { model }),
        _ => None,
    }
}

/// # Safety
/// `core` и `config` должны быть валидными указателями, строки внутри
/// конфигурации — живыми на время вызова.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_llm(core: *mut Core, config: *const FfiLlmConfig) {
    let Some(core) = core.as_ref() else { return };

    let provider = config.as_ref().and_then(provider_from_config);

    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut slot) = core.provider.lock() {
            *slot = provider;
        }
    }));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_llm_enabled(core: *const Core) -> u8 {
    let Some(core) = core.as_ref() else { return 0 };
    core.provider
        .lock()
        .map(|slot| u8::from(slot.is_some()))
        .unwrap_or(0)
}

/// # Safety
/// `core` и `out_request` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_build_llm_request(
    core: *mut Core,
    out_request: *mut FfiLlmRequest,
) -> c_int {
    let (Some(core), Some(slot)) = (core.as_ref(), out_request.as_mut()) else {
        return -1;
    };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let provider = core.provider.lock().ok()?.clone()?;
        let request = (*core.last_request.lock().ok()?)?;
        Some(llm::build_request(&provider, &request))
    }));

    match outcome {
        Ok(Some(plan)) => {
            // Не влезло — значит, настройки пользователя неправдоподобно
            // длинные. Лучше не отправить ничего, чем отправить обрезок.
            if plan.url.len() >= LLM_URL_SIZE || plan.body.len() >= LLM_BODY_SIZE {
                core.log(LOG_WARNING, "запрос к LLM не помещается в буфер");
                return 0;
            }

            slot.url = [0; LLM_URL_SIZE];
            slot.body = [0; LLM_BODY_SIZE];
            fill_utf8(&mut slot.url, &plan.url);
            fill_utf8(&mut slot.body, &plan.body);
            slot.timeout_ms = plan.timeout_ms;
            1
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core`, `archive` и `out_report` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_install_pack(
    core: *mut Core,
    archive: *const c_char,
    archive_len: usize,
    out_report: *mut c_char,
    report_size: usize,
) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };
    if archive.is_null() {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let bytes = std::slice::from_raw_parts(archive as *const u8, archive_len);
        let mut packs = core.packs.lock().ok()?;
        Some(packs.install(bytes, &ArchiveLimits::default(), &Limits::default()))
    }));

    // Замечания собираются и при успехе, и при отказе: §US-07 требует
    // объяснить, почему пакет отклонён, а предупреждения полезны и у принятого.
    let (accepted, findings) = match outcome {
        Ok(Some(Ok(findings))) => (true, findings),
        Ok(Some(Err(findings))) => (false, findings),
        Ok(None) => (false, Vec::new()),
        Err(_) => {
            core.log(LOG_WARNING, "паника при установке пакета перехвачена");
            return -2;
        }
    };

    if !out_report.is_null() && report_size > 0 {
        let text = findings
            .iter()
            .map(|finding| finding.message.clone())
            .collect::<Vec<_>>()
            .join("\n");

        let buffer = std::slice::from_raw_parts_mut(out_report, report_size);
        buffer.fill(0);
        fill_utf8(buffer, &text);
    }

    c_int::from(accepted)
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_rollback_pack(core: *mut Core) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut packs) = core.packs.lock() {
            packs.rollback();
        }
    }));
}

/// Размер листа, ожидающего записи на диск. Ноль означает, что записывать
/// нечего: либо активен встроенный питомец, либо лист уже забрали.
///
/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_pending_sheet_size(core: *mut Core) -> usize {
    let Some(core) = core.as_ref() else { return 0 };
    core.packs
        .lock()
        .map(|packs| packs.active().pending_sheet_len())
        .unwrap_or(0)
}

/// Забирает байты листа. После успешного вызова ядро их не хранит.
///
/// # Safety
/// `out` должен быть буфером не меньше `size` байт.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_take_sheet(
    core: *mut Core,
    out: *mut c_char,
    size: usize,
) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };
    if out.is_null() {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let mut packs = core.packs.lock().ok()?;
        packs.active_mut().take_sheet()
    }));

    match outcome {
        Ok(Some(bytes)) => {
            if bytes.len() > size {
                // Буфер меньше листа: лучше не отдать ничего, чем отдать
                // обрезанный PNG, который хост запишет как рабочий.
                return -1;
            }
            let buffer = std::slice::from_raw_parts_mut(out as *mut u8, bytes.len());
            buffer.copy_from_slice(&bytes);
            c_int::try_from(bytes.len()).unwrap_or(c_int::MAX)
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core` и оба буфера должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_active_pack(
    core: *mut Core,
    out_id: *mut c_char,
    id_size: usize,
    out_sheet_file: *mut c_char,
    sheet_size: usize,
) {
    let Some(core) = core.as_ref() else { return };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Ok(packs) = core.packs.lock() else { return };
        let pack = packs.active();

        if !out_id.is_null() && id_size > 0 {
            let buffer = std::slice::from_raw_parts_mut(out_id, id_size);
            buffer.fill(0);
            fill_utf8(buffer, pack.id());
        }

        if !out_sheet_file.is_null() && sheet_size > 0 {
            let buffer = std::slice::from_raw_parts_mut(out_sheet_file, sheet_size);
            buffer.fill(0);
            if let SheetSource::Imported { file } = pack.source() {
                fill_utf8(buffer, file);
            }
        }
    }));
}

/// # Safety
/// `core`, `adc` и `out_request` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_build_token_request(
    core: *mut Core,
    adc: *const c_char,
    adc_len: usize,
    out_request: *mut FfiLlmRequest,
) -> c_int {
    let (Some(_core), Some(slot)) = (core.as_ref(), out_request.as_mut()) else {
        return -1;
    };
    if adc.is_null() {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let bytes = std::slice::from_raw_parts(adc as *const u8, adc_len);
        llm::parse_adc(bytes).map(|credentials| llm::build_token_request(&credentials))
    }));

    match outcome {
        Ok(Ok(plan)) => {
            if plan.url.len() >= LLM_URL_SIZE || plan.body.len() >= LLM_BODY_SIZE {
                return 0;
            }
            slot.url = [0; LLM_URL_SIZE];
            slot.body = [0; LLM_BODY_SIZE];
            fill_utf8(&mut slot.url, &plan.url);
            fill_utf8(&mut slot.body, &plan.body);
            slot.timeout_ms = plan.timeout_ms;
            1
        }
        // Отдельный код: пользователю нужно понять, что чинить не файл,
        // а способ входа.
        Ok(Err(llm::AdcError::ServiceAccountUnsupported)) => -3,
        Ok(Err(_)) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core`, `raw`, `out_token` и `out_expires_seconds` должны быть валидными.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_accept_token_response(
    core: *mut Core,
    raw: *const c_char,
    raw_len: usize,
    out_token: *mut c_char,
    token_size: usize,
    out_expires_seconds: *mut u32,
) -> c_int {
    let Some(_core) = core.as_ref() else {
        return -1;
    };
    if raw.is_null() || out_token.is_null() || token_size == 0 {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let bytes = std::slice::from_raw_parts(raw as *const u8, raw_len);
        llm::parse_token_response(bytes)
    }));

    match outcome {
        Ok(Some((token, expires))) => {
            if token.len() >= token_size {
                return 0;
            }
            let buffer = std::slice::from_raw_parts_mut(out_token, token_size);
            buffer.fill(0);
            fill_utf8(buffer, &token);
            if let Some(slot) = out_expires_seconds.as_mut() {
                *slot = expires;
            }
            1
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core` и `out_request` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_build_health_request(
    core: *mut Core,
    out_request: *mut FfiLlmRequest,
) -> c_int {
    let (Some(core), Some(slot)) = (core.as_ref(), out_request.as_mut()) else {
        return -1;
    };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let provider = core.provider.lock().ok()?.clone()?;
        Some(llm::build_health_request(&provider))
    }));

    match outcome {
        Ok(Some(plan)) => {
            if plan.url.len() >= LLM_URL_SIZE {
                return 0;
            }
            slot.url = [0; LLM_URL_SIZE];
            slot.body = [0; LLM_BODY_SIZE];
            fill_utf8(&mut slot.url, &plan.url);
            slot.timeout_ms = plan.timeout_ms;
            1
        }
        Ok(None) => 0,
        Err(_) => -2,
    }
}

/// # Safety
/// `core` и `raw` должны быть валидными указателями.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_accept_health_response(
    core: *mut Core,
    raw: *const c_char,
    raw_len: usize,
) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };
    if raw.is_null() {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let provider = core.provider.lock().ok()?.clone()?;
        let bytes = std::slice::from_raw_parts(raw as *const u8, raw_len);
        let models = llm::parse_health_response(&provider, bytes).ok()?;
        Some(llm::model_is_present(&provider, &models))
    }));

    match outcome {
        Ok(Some(true)) => 1,
        Ok(Some(false)) => 0,
        Ok(None) => -1,
        Err(_) => -2,
    }
}

/// # Safety
/// `core`, `raw` и `out_phrase` должны быть валидными указателями,
/// `out_phrase` — буфером на `out_size` байт.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_accept_llm_response(
    core: *mut Core,
    raw: *const c_char,
    raw_len: usize,
    out_phrase: *mut c_char,
    out_size: usize,
) -> c_int {
    let Some(core) = core.as_ref() else { return -1 };
    if raw.is_null() || out_phrase.is_null() || out_size == 0 {
        return -1;
    }

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let provider = core.provider.lock().ok()?.clone()?;
        let bytes = std::slice::from_raw_parts(raw as *const u8, raw_len);
        llm::parse_response(&provider, bytes).ok()
    }));

    match outcome {
        Ok(Some(phrase)) => {
            let buffer = std::slice::from_raw_parts_mut(out_phrase, out_size);
            buffer.fill(0);
            fill_utf8(buffer, &phrase);
            1
        }
        // Негодный ответ — не ошибка вызова: хост показывает шаблон (§FR-6).
        Ok(None) => 0,
        Err(_) => -2,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn empty_event(kind: u32) -> FfiEvent {
        FfiEvent {
            kind,
            idle_seconds: 0,
            on_battery: 0,
            battery_percent_valid: 0,
            battery_percent: 0,
            power_state: 0,
            session_state: 0,
            media_state: 0,
            app_id: std::ptr::null(),
            app_id_len: 0,
            category: std::ptr::null(),
            category_len: 0,
        }
    }

    #[test]
    fn click_crosses_the_boundary() {
        let core = openpet_core_new();
        assert!(!core.is_null());

        let event = empty_event(EVENT_PET_CLICKED);
        let mut reaction = to_ffi(
            &Reaction {
                emotion: Emotion::Idle,
                animation: Emotion::Idle,
                phrase_intent: None,
                priority: 0,
                ttl_ms: 0,
                cooldown_key: None,
            },
            None,
        );

        unsafe {
            assert_eq!(openpet_core_push_event(core, &event, &mut reaction), 1);
            assert_eq!(reaction.emotion, emotion_code(Emotion::Happy));
            assert_eq!(
                openpet_core_current_emotion(core),
                emotion_code(Emotion::Happy)
            );
            openpet_core_free(core);
        }
    }

    #[test]
    fn unknown_kind_is_rejected_not_crashed() {
        let core = openpet_core_new();
        let event = empty_event(9999);
        unsafe {
            assert_eq!(
                openpet_core_push_event(core, &event, std::ptr::null_mut()),
                -1
            );
            openpet_core_free(core);
        }
    }

    #[test]
    fn null_core_is_an_error_not_a_crash() {
        let event = empty_event(EVENT_PET_CLICKED);
        unsafe {
            assert_eq!(
                openpet_core_push_event(std::ptr::null_mut(), &event, std::ptr::null_mut()),
                -1
            );
            openpet_core_free(std::ptr::null_mut());
        }
    }

    #[test]
    fn invalid_utf8_app_id_does_not_crash() {
        let core = openpet_core_new();
        let invalid: [u8; 3] = [0xff, 0xfe, 0xfd];

        let mut event = empty_event(EVENT_ACTIVE_APP_CHANGED);
        event.app_id = invalid.as_ptr() as *const c_char;
        event.app_id_len = invalid.len();

        unsafe {
            // Строка отбрасывается, событие становится «без app id»
            // и по правилам §FR-5 реакции не даёт.
            assert_eq!(
                openpet_core_push_event(core, &event, std::ptr::null_mut()),
                0
            );
            openpet_core_free(core);
        }
    }

    fn phrase_of(reaction: &FfiReaction) -> String {
        let bytes: Vec<u8> = reaction
            .phrase
            .iter()
            .take_while(|c| **c != 0)
            .map(|c| *c as u8)
            .collect();
        String::from_utf8(bytes).expect("реплика обязана остаться валидным UTF-8")
    }

    #[test]
    fn phrase_crosses_the_boundary_in_russian() {
        let core = openpet_core_new();
        let tag = "ru_RU.UTF-8";
        let event = empty_event(EVENT_PET_CLICKED);
        let mut reaction = to_ffi(
            &Reaction {
                emotion: Emotion::Idle,
                animation: Emotion::Idle,
                phrase_intent: None,
                priority: 0,
                ttl_ms: 0,
                cooldown_key: None,
            },
            None,
        );

        unsafe {
            openpet_core_set_locale(core, tag.as_ptr() as *const c_char, tag.len());
            assert_eq!(openpet_core_push_event(core, &event, &mut reaction), 1);
            assert_eq!(reaction.has_phrase, 1);

            let text = phrase_of(&reaction);
            assert!(!text.is_empty());
            assert!(
                text.chars().any(|c| ('а'..='я').contains(&c)),
                "на русской локали ожидался русский текст, получено: {text}"
            );

            openpet_core_free(core);
        }
    }

    #[test]
    fn locking_produces_no_reaction() {
        let core = openpet_core_new();
        // Блокировка экрана: питомца за ней не видно, поэтому реакции нет
        // вовсе — ни позы, ни реплики.
        let mut event = empty_event(EVENT_SESSION_CHANGED);
        event.session_state = 1;

        unsafe {
            assert_eq!(
                openpet_core_push_event(core, &event, std::ptr::null_mut()),
                0
            );
            assert_eq!(
                openpet_core_current_emotion(core),
                emotion_code(Emotion::Idle)
            );
            openpet_core_free(core);
        }
    }

    #[test]
    fn reaction_without_intent_carries_no_phrase() {
        let raw = to_ffi(
            &Reaction {
                emotion: Emotion::Idle,
                animation: Emotion::Idle,
                phrase_intent: None,
                priority: 0,
                ttl_ms: 0,
                cooldown_key: None,
            },
            None,
        );
        assert_eq!(raw.has_phrase, 0);
        assert!(phrase_of(&raw).is_empty());
    }

    #[test]
    fn truncation_keeps_utf8_valid() {
        // Буфер на 8 байт: русская фраза заведомо не влезает и обрежется
        // посреди символа, если не следить за границами.
        let mut buffer = [0 as c_char; 8];
        fill_utf8(&mut buffer, "Заряжаемся!");
        let bytes: Vec<u8> = buffer
            .iter()
            .take_while(|c| **c != 0)
            .map(|c| *c as u8)
            .collect();
        let text = String::from_utf8(bytes).expect("обрезка не должна ломать UTF-8");
        assert!("Заряжаемся!".starts_with(&text));
    }

    #[test]
    fn pause_is_observable_through_the_boundary() {
        let core = openpet_core_new();
        let event = empty_event(EVENT_PET_CLICKED);

        unsafe {
            openpet_core_set_paused(core, 1);
            assert_eq!(openpet_core_is_paused(core), 1);
            assert_eq!(
                openpet_core_push_event(core, &event, std::ptr::null_mut()),
                0
            );

            openpet_core_set_paused(core, 0);
            assert_eq!(
                openpet_core_push_event(core, &event, std::ptr::null_mut()),
                1
            );
            openpet_core_free(core);
        }
    }
}
