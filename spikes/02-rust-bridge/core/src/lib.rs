//! Слой FFI. Вся работа здесь — перевод между C-структурами и доменом
//! из [`behavior`], плюс защита хоста от паники.
//!
//! Домен об этом слое ничего не знает: если завтра мост поменяется,
//! выбрасывается только этот файл.

mod behavior;

use behavior::{DesktopEvent, Emotion, Reaction, StateMachine};

use std::os::raw::{c_char, c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

pub const ABI_VERSION: u32 = 1;
const COOLDOWN_KEY_SIZE: usize = 32;

const EVENT_ACTIVITY_RESUMED: u32 = 0;
const EVENT_IDLE_THRESHOLD_REACHED: u32 = 1;
const EVENT_POWER_CHANGED: u32 = 2;
const EVENT_ACTIVE_APP_CHANGED: u32 = 3;
const EVENT_PET_CLICKED: u32 = 4;

#[repr(C)]
pub struct FfiEvent {
    kind: u32,
    idle_seconds: u32,
    on_battery: u8,
    battery_percent_valid: u8,
    battery_percent: u8,
    power_state: u32,
    app_id: *const c_char,
    app_id_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiReaction {
    emotion: u32,
    animation: u32,
    phrase_intent: u32,
    phrase_intent_valid: u8,
    priority: u8,
    ttl_ms: u32,
    cooldown_key: [c_char; COOLDOWN_KEY_SIZE],
}

pub type ReactionCallback = extern "C" fn(*const FfiReaction, *mut c_void);

/// Указатель на данные хоста, приходящий вместе с callback. `*mut c_void`
/// не является `Send`, поэтому обёртка нужна, чтобы отдать его в поток тикера.
/// Обязательство хоста: указатель живёт не меньше, чем ядро.
struct CallbackTarget {
    callback: ReactionCallback,
    user_data: *mut c_void,
}

// `Arc<T>: Send` требует ещё и `Sync`. Обещание, которое здесь даётся:
// ядро только передаёт указатель обратно в callback, никогда его не разыменовывая,
// а безопасность самого вызова из чужого потока обеспечивает хост.
unsafe impl Send for CallbackTarget {}
unsafe impl Sync for CallbackTarget {}

pub struct Core {
    machine: Mutex<StateMachine>,
    target: Mutex<Option<Arc<CallbackTarget>>>,
    ticker_running: Arc<AtomicBool>,
}

impl Core {
    fn new() -> Self {
        Self {
            machine: Mutex::new(StateMachine::new()),
            target: Mutex::new(None),
            ticker_running: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl Drop for Core {
    fn drop(&mut self) {
        self.ticker_running.store(false, Ordering::SeqCst);
    }
}

fn emotion_code(emotion: Emotion) -> u32 {
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
        // Обрезаем молча: ключ cooldown — служебная строка, которую формирует
        // само ядро, а не пользовательские данные.
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
        // Анимация в спайке повторяет эмоцию: выбор анимации — задача Pet Pack,
        // а не моста.
        animation: emotion_code(reaction.emotion),
        phrase_intent: emotion_code(reaction.emotion),
        phrase_intent_valid: 1,
        priority: reaction.priority,
        ttl_ms: reaction.ttl_ms,
        cooldown_key,
    }
}

/// # Safety
/// `event` должен указывать на валидную структуру, а `app_id` — на строку
/// длиной `app_id_len` байт, живую на время вызова.
unsafe fn to_domain(event: &FfiEvent) -> Option<DesktopEvent> {
    let domain = match event.kind {
        EVENT_ACTIVITY_RESUMED => DesktopEvent::ActivityResumed,
        EVENT_IDLE_THRESHOLD_REACHED => DesktopEvent::IdleThresholdReached {
            seconds: event.idle_seconds,
        },
        EVENT_POWER_CHANGED => DesktopEvent::PowerChanged {
            on_battery: event.on_battery != 0,
            percent: if event.battery_percent_valid != 0 {
                Some(event.battery_percent)
            } else {
                None
            },
        },
        EVENT_ACTIVE_APP_CHANGED => {
            let app_id = if event.app_id.is_null() || event.app_id_len == 0 {
                None
            } else {
                let bytes =
                    std::slice::from_raw_parts(event.app_id as *const u8, event.app_id_len);
                // Невалидный UTF-8 не роняет ядро: app id приходит извне,
                // и доверять его кодировке нельзя.
                std::str::from_utf8(bytes).ok().map(|s| s.to_string())
            };
            DesktopEvent::ActiveAppChanged { app_id }
        }
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
/// `core` должен быть указателем, полученным из [`openpet_core_new`],
/// и не должен использоваться после этого вызова.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_free(core: *mut Core) {
    if core.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        drop(Box::from_raw(core));
    }));
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_set_callback(
    core: *mut Core,
    callback: Option<ReactionCallback>,
    user_data: *mut c_void,
) {
    let Some(core) = core.as_ref() else {
        return;
    };

    let mut target = core.target.lock().unwrap();
    *target = callback.map(|callback| {
        Arc::new(CallbackTarget {
            callback,
            user_data,
        })
    });
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

    let result = catch_unwind(AssertUnwindSafe(|| {
        let domain = to_domain(event)?;
        let mut machine = core.machine.lock().ok()?;
        machine.handle(domain)
    }));

    match result {
        Ok(Some(reaction)) => {
            if let Some(slot) = out_reaction.as_mut() {
                *slot = to_ffi(&reaction);
            }
            1
        }
        Ok(None) => 0,
        // Паника не должна пересекать границу FFI: за ней начинается C++,
        // где разворачивание стека — неопределённое поведение.
        Err(_) => -2,
    }
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_start_ticker(core: *mut Core, interval_ms: u32) {
    let Some(core_ref) = core.as_ref() else {
        return;
    };

    if core_ref.ticker_running.swap(true, Ordering::SeqCst) {
        return;
    }

    let running = Arc::clone(&core_ref.ticker_running);
    let target = core_ref.target.lock().unwrap().clone();
    let Some(target) = target else {
        running.store(false, Ordering::SeqCst);
        return;
    };

    let interval = Duration::from_millis(u64::from(interval_ms.max(1)));

    thread::spawn(move || {
        let mut machine = StateMachine::new();
        let mut tick: u32 = 0;

        while running.load(Ordering::SeqCst) {
            thread::sleep(interval);
            if !running.load(Ordering::SeqCst) {
                break;
            }

            tick += 1;
            let event = if tick % 2 == 0 {
                DesktopEvent::ActivityResumed
            } else {
                DesktopEvent::IdleThresholdReached { seconds: 300 }
            };

            if let Some(reaction) = machine.handle(event) {
                let ffi = to_ffi(&reaction);
                // Callback уходит из потока ядра. Перекладывание в поток UI —
                // обязанность хоста, ядро об этом ничего не знает.
                (target.callback)(&ffi as *const FfiReaction, target.user_data);
            }
        }
    });
}

/// # Safety
/// `core` должен быть валидным указателем на ядро.
#[no_mangle]
pub unsafe extern "C" fn openpet_core_simulate_panic(core: *mut Core) -> c_int {
    if core.is_null() {
        return -1;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        panic!("намеренная паника для проверки границы FFI");
    }));

    match result {
        Ok(()) => 0,
        Err(_) => -2,
    }
}
