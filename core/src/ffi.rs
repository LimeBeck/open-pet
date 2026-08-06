//! Слой FFI: перевод между C-структурами из `platform/contracts` и доменом.
//!
//! Здесь нет ни правил поведения, ни знания о Qt — только перевод и защита
//! хоста от паники. При смене способа связывания выбрасывается этот файл
//! и C++-обёртка; домен не затрагивается (ADR-001).

use crate::behavior::{Reaction, StateMachine, Suppressed};
use crate::emotion::Emotion;
use crate::event::{DesktopEvent, MediaState, PowerState, SessionState};

use std::os::raw::{c_char, c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

pub const ABI_VERSION: u32 = 1;
const COOLDOWN_KEY_SIZE: usize = 32;

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

pub struct Core {
    machine: Mutex<StateMachine>,
    callbacks: Mutex<Callbacks>,
}

impl Core {
    fn new() -> Self {
        Self {
            machine: Mutex::new(StateMachine::new()),
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

fn to_ffi(reaction: &Reaction) -> FfiReaction {
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

    FfiReaction {
        emotion: emotion_code(reaction.emotion),
        animation: emotion_code(reaction.animation),
        priority: reaction.priority,
        ttl_ms: reaction.ttl_ms,
        cooldown_key,
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
            if let Some(slot) = out_reaction.as_mut() {
                *slot = to_ffi(&reaction);
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
        let mut reaction = to_ffi(&Reaction {
            emotion: Emotion::Idle,
            animation: Emotion::Idle,
            priority: 0,
            ttl_ms: 0,
            cooldown_key: None,
        });

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
