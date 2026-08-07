//! Доменное ядро open-pet.
//!
//! Ядро не знает ни о Qt, ни о Wayland, ни о D-Bus: адаптеры приводят
//! платформенные события к [`event::DesktopEvent`], а обратно получают
//! [`behavior::Reaction`]. Это условие переносимости на другие платформы
//! без переписывания логики (гипотеза 4 из §1 спецификации).
//!
//! Слой C ABI живёт в [`ffi`] и является единственным местом, которое
//! придётся переписать при смене способа связывания (ADR-001).

pub mod behavior;
pub mod emotion;
pub mod event;
pub mod ffi;
pub mod phrase;

pub use behavior::{Reaction, StateMachine, Suppressed};
pub use emotion::Emotion;
pub use event::{CapabilityState, DesktopEvent, MediaState, PowerState, SessionState};
pub use phrase::{Locale, Phrase, PhraseBook, PhraseIntent};
