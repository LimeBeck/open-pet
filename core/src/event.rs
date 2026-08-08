//! Нормализованная модель событий (§FR-4).
//!
//! Это единственное, что адаптеры вправе сообщать ядру. Всё, чего здесь нет,
//! ядро получить не может — не потому, что фильтруется, а потому, что
//! не выразимо в этом типе.

/// Состояние питания.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PowerState {
    Unknown,
    Charging,
    Discharging,
    Full,
}

/// Состояние сессии.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionState {
    Active,
    Locked,
    Sleeping,
    Resumed,
}

/// Состояние воспроизведения медиа. Ровно три значения — ни громкости,
/// ни названия трека, ни исполнителя (§4.2).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MediaState {
    Stopped,
    Playing,
    Paused,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DesktopEvent {
    ActivityResumed,
    IdleThresholdReached {
        seconds: u32,
    },
    PowerChanged {
        on_battery: bool,
        percent: Option<u8>,
        state: PowerState,
    },
    SessionChanged {
        state: SessionState,
    },
    /// Только нормализованный идентификатор приложения. Заголовка окна
    /// и содержимого документа здесь нет и быть не может (§4.2).
    ActiveAppChanged {
        app_id: Option<String>,
    },
    /// Только факт события и необязательная категория. Ни текста,
    /// ни отправителя, ни вложений (§4.2).
    NotificationOccurred {
        category: Option<String>,
    },
    MediaChanged {
        state: MediaState,
    },
    PetClicked,
    /// Питомца потащили курсором.
    ///
    /// Отдельно от клика: перетаскивание — это намеренное обращение
    /// с питомцем, и реагировать на него как на клик значит терять
    /// разницу между «погладили» и «переставили».
    PetDragged,
}

/// Состояние здоровья источника событий (§FR-4).
///
/// Недоступность источника — штатная ситуация: приложение продолжает работать
/// без соответствующей capability (§10).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CapabilityState {
    Available,
    PermissionRequired,
    Unsupported,
    Degraded,
}
