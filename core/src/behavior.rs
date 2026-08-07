//! Rule engine (§FR-5).
//!
//! Правила детерминированы и тестируются без UI: время передаётся снаружи,
//! состояние целиком принадлежит [`StateMachine`]. Ни Qt, ни C здесь нет —
//! это условие переносимости ядра на другие платформы (гипотеза 4 из §1).

use crate::emotion::Emotion;
use crate::event::{DesktopEvent, MediaState, PowerState, SessionState};
use crate::phrase::PhraseIntent;

use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Длительность cooldown по умолчанию. Защита от спама одинаковыми репликами
/// на поток событий (§10).
const DEFAULT_COOLDOWN: Duration = Duration::from_secs(30);

/// Реакция ядра на событие (§FR-5).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reaction {
    pub emotion: Emotion,
    /// В MVP анимация совпадает с эмоцией: сопоставление состояния и файла —
    /// задача Pet Pack (§FR-8), а не rule engine.
    pub animation: Emotion,
    /// Что питомец хочет сказать. Именно намерение, а не текст: выбор
    /// формулировки — дело каталога реплик или LLM (§FR-6, §US-06).
    /// `None` означает «промолчать»: не каждая смена позы заслуживает слов.
    pub phrase_intent: Option<PhraseIntent>,
    pub priority: u8,
    pub ttl_ms: u32,
    pub cooldown_key: Option<String>,
}

impl Reaction {
    fn new(
        emotion: Emotion,
        intent: Option<PhraseIntent>,
        ttl_ms: u32,
        cooldown_key: Option<&str>,
    ) -> Self {
        Self {
            emotion,
            animation: emotion,
            phrase_intent: intent,
            priority: emotion.priority(),
            ttl_ms,
            cooldown_key: cooldown_key.map(str::to_string),
        }
    }
}

/// Почему событие не дало реакции. Нужно для диагностики: «ничего не произошло»
/// — самый неудобный симптом при разборе жалоб на поведение питомца.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Suppressed {
    /// Реакции приостановлены пользователем (§FR-2).
    Paused,
    /// Событие не порождает реакции в текущем состоянии.
    NoRule,
    /// Ключ cooldown ещё не истёк.
    Cooldown,
    /// Активно более приоритетное состояние.
    LowerPriority,
}

pub struct StateMachine {
    current: Emotion,
    current_until: Option<Instant>,
    cooldowns: HashMap<String, Instant>,
    cooldown_duration: Duration,
    low_battery_threshold: u8,
    paused: bool,
    /// Последнее известное состояние медиа — определяет, занят ли пользователь.
    media: MediaState,
}

impl Default for StateMachine {
    fn default() -> Self {
        Self::new()
    }
}

impl StateMachine {
    pub fn new() -> Self {
        Self {
            current: Emotion::Idle,
            current_until: None,
            cooldowns: HashMap::new(),
            cooldown_duration: DEFAULT_COOLDOWN,
            low_battery_threshold: 20,
            paused: false,
            media: MediaState::Stopped,
        }
    }

    pub fn current_emotion(&self) -> Emotion {
        self.current
    }

    pub fn is_paused(&self) -> bool {
        self.paused
    }

    pub fn set_paused(&mut self, paused: bool) {
        self.paused = paused;
    }

    pub fn set_low_battery_threshold(&mut self, percent: u8) {
        self.low_battery_threshold = percent.min(100);
    }

    pub fn set_cooldown_duration(&mut self, duration: Duration) {
        self.cooldown_duration = duration;
    }

    pub fn handle(&mut self, event: DesktopEvent) -> Result<Reaction, Suppressed> {
        self.handle_at(event, Instant::now())
    }

    /// Время передаётся снаружи, чтобы правила тестировались без ожидания.
    /// Тесты, полагающиеся на «сколько-то событий за столько-то миллисекунд»,
    /// врут в обе стороны — это выяснилось ещё на спайке ADR-001.
    pub fn handle_at(&mut self, event: DesktopEvent, now: Instant) -> Result<Reaction, Suppressed> {
        if self.paused {
            // Подавление живёт в ядре, а не в каждом адаптере: правило должно
            // быть одно и то же для всех источников.
            return Err(Suppressed::Paused);
        }

        // Состояние медиа обновляется всегда, даже когда само событие
        // не порождает реакции: оно влияет на трактовку следующих.
        if let DesktopEvent::MediaChanged { state } = event {
            self.media = state;
        }

        let candidate = self.candidate_for(&event).ok_or(Suppressed::NoRule)?;

        if let Some(key) = &candidate.cooldown_key {
            if let Some(until) = self.cooldowns.get(key) {
                if now < *until {
                    return Err(Suppressed::Cooldown);
                }
            }
        }

        // Более приоритетное состояние временно прерывает менее приоритетное;
        // равное или меньшее ждёт истечения ttl текущего (§FR-5).
        let current_active = self.current_until.is_some_and(|until| now < until);
        if current_active && candidate.priority <= self.current.priority() {
            return Err(Suppressed::LowerPriority);
        }

        self.current = candidate.emotion;
        self.current_until = Some(now + Duration::from_millis(u64::from(candidate.ttl_ms)));

        if let Some(key) = &candidate.cooldown_key {
            self.cooldowns
                .insert(key.clone(), now + self.cooldown_duration);
        }

        Ok(candidate)
    }

    /// Возвращает эмоцию, в которую следует вернуться, если ttl текущей истёк.
    /// Питомец не залипает в состоянии навсегда: он засыпает или возвращается
    /// в покой.
    pub fn settle_at(&mut self, now: Instant) -> Option<Emotion> {
        let expired = self.current_until.is_some_and(|until| now >= until);
        if !expired || self.current == Emotion::Idle {
            return None;
        }

        self.current = Emotion::Idle;
        self.current_until = None;
        Some(Emotion::Idle)
    }

    fn candidate_for(&self, event: &DesktopEvent) -> Option<Reaction> {
        let reaction = match event {
            DesktopEvent::ActivityResumed => Reaction::new(
                Emotion::Happy,
                Some(PhraseIntent::WelcomeBack),
                4_000,
                Some("activity_resumed"),
            ),

            DesktopEvent::IdleThresholdReached { .. } => {
                // Сон не имеет cooldown: это устойчивое состояние, а не всплеск.
                Reaction::new(
                    Emotion::Sleepy,
                    Some(PhraseIntent::GettingSleepy),
                    3_600_000,
                    None,
                )
            }

            DesktopEvent::PowerChanged {
                on_battery,
                percent,
                state,
            } => {
                let low = percent.is_some_and(|value| value <= self.low_battery_threshold);

                if *on_battery && low {
                    Reaction::new(
                        Emotion::LowBattery,
                        Some(PhraseIntent::LowBattery),
                        8_000,
                        Some("low_battery"),
                    )
                } else if !*on_battery && matches!(state, PowerState::Charging | PowerState::Full) {
                    Reaction::new(
                        Emotion::Charging,
                        Some(PhraseIntent::Charging),
                        5_000,
                        Some("charging"),
                    )
                } else {
                    // Разряд без пересечения порога — не событие для питомца.
                    // Повторные обновления одного состояния не создают
                    // новые реплики (§US-04).
                    return None;
                }
            }

            DesktopEvent::SessionChanged { state } => match state {
                // Заблокированный экран и спящая машина не дают реакции вовсе.
                //
                // Питомца в этот момент не видно: поверх лежит экран блокировки,
                // а при разблокировке сразу приходит Resumed и показывает happy —
                // промежуточная поза не появляется ни на кадр. Менять состояние
                // ради невидимой смены значит врать самим себе в диагностике.
                //
                // Ценность этих событий в другом: хост по ним приостанавливает
                // отрисовку, иначе питомец крутит анимацию за экраном блокировки
                // и жжёт батарею (§14).
                SessionState::Sleeping | SessionState::Locked => return None,
                SessionState::Resumed => Reaction::new(
                    Emotion::Happy,
                    Some(PhraseIntent::WelcomeBack),
                    4_000,
                    Some("session_resumed"),
                ),
                SessionState::Active => return None,
            },

            DesktopEvent::ActiveAppChanged { app_id } => {
                // Идентификатор приложения на выбор эмоции не влияет: питомец
                // замечает сам факт смены контекста. Разбор конкретных
                // приложений означал бы профилирование пользователя.
                if app_id.is_none() {
                    return None;
                }

                if self.media == MediaState::Playing {
                    Reaction::new(
                        Emotion::Busy,
                        Some(PhraseIntent::NoticedContext),
                        3_000,
                        Some("active_app"),
                    )
                } else {
                    Reaction::new(
                        Emotion::Curious,
                        Some(PhraseIntent::NoticedContext),
                        3_000,
                        Some("active_app"),
                    )
                }
            }

            DesktopEvent::NotificationOccurred { .. } => Reaction::new(
                Emotion::Notification,
                Some(PhraseIntent::NoticedNotification),
                3_500,
                Some("notification"),
            ),

            DesktopEvent::MediaChanged { state } => match state {
                MediaState::Playing => Reaction::new(
                    Emotion::Busy,
                    Some(PhraseIntent::MediaStarted),
                    6_000,
                    Some("media"),
                ),
                // Остановка и пауза сами по себе реакции не заслуживают:
                // иначе питомец дёргается на каждый трек.
                MediaState::Paused | MediaState::Stopped => return None,
            },

            DesktopEvent::PetClicked => {
                // Клик — единственное намеренное обращение пользователя,
                // поэтому cooldown короче общего (§FR-2).
                Reaction::new(
                    Emotion::Happy,
                    Some(PhraseIntent::Petted),
                    2_500,
                    Some("pet_clicked"),
                )
            }
        };

        Some(reaction)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn at(base: Instant, secs: u64) -> Instant {
        base + Duration::from_secs(secs)
    }

    #[test]
    fn click_makes_pet_happy() {
        let mut machine = StateMachine::new();
        let reaction = machine.handle(DesktopEvent::PetClicked).unwrap();
        assert_eq!(reaction.emotion, Emotion::Happy);
        assert_eq!(machine.current_emotion(), Emotion::Happy);
    }

    #[test]
    fn repeated_click_is_suppressed_by_cooldown() {
        let mut machine = StateMachine::new();
        let base = Instant::now();

        assert!(machine.handle_at(DesktopEvent::PetClicked, base).is_ok());
        assert_eq!(
            machine.handle_at(DesktopEvent::PetClicked, at(base, 1)),
            Err(Suppressed::Cooldown)
        );
        assert!(machine
            .handle_at(DesktopEvent::PetClicked, at(base, 31))
            .is_ok());
    }

    #[test]
    fn low_battery_interrupts_sleepy() {
        let mut machine = StateMachine::new();
        let base = Instant::now();

        machine
            .handle_at(DesktopEvent::IdleThresholdReached { seconds: 300 }, base)
            .unwrap();
        assert_eq!(machine.current_emotion(), Emotion::Sleepy);

        let reaction = machine
            .handle_at(
                DesktopEvent::PowerChanged {
                    on_battery: true,
                    percent: Some(9),
                    state: PowerState::Discharging,
                },
                base,
            )
            .unwrap();

        assert_eq!(reaction.emotion, Emotion::LowBattery);
    }

    #[test]
    fn lower_priority_waits_for_ttl() {
        let mut machine = StateMachine::new();
        let base = Instant::now();

        machine
            .handle_at(
                DesktopEvent::PowerChanged {
                    on_battery: true,
                    percent: Some(5),
                    state: PowerState::Discharging,
                },
                base,
            )
            .unwrap();

        // low_battery держится 8 секунд; уведомление приоритетом ниже.
        assert_eq!(
            machine.handle_at(
                DesktopEvent::NotificationOccurred { category: None },
                at(base, 1)
            ),
            Err(Suppressed::LowerPriority)
        );

        // После истечения ttl то же событие проходит.
        assert!(machine
            .handle_at(
                DesktopEvent::NotificationOccurred { category: None },
                at(base, 9)
            )
            .is_ok());
    }

    #[test]
    fn discharging_above_threshold_is_silent() {
        let mut machine = StateMachine::new();
        assert_eq!(
            machine.handle(DesktopEvent::PowerChanged {
                on_battery: true,
                percent: Some(80),
                state: PowerState::Discharging,
            }),
            Err(Suppressed::NoRule)
        );
    }

    #[test]
    fn media_playing_turns_app_switch_into_busy() {
        let mut machine = StateMachine::new();
        let base = Instant::now();

        machine
            .handle_at(
                DesktopEvent::MediaChanged {
                    state: MediaState::Playing,
                },
                base,
            )
            .unwrap();

        // Busy держится 6 секунд — ждём, чтобы проверялось правило,
        // а не приоритет.
        let reaction = machine
            .handle_at(
                DesktopEvent::ActiveAppChanged {
                    app_id: Some("org.kde.konsole".to_string()),
                },
                at(base, 7),
            )
            .unwrap();

        assert_eq!(reaction.emotion, Emotion::Busy);
    }

    #[test]
    fn app_switch_without_media_is_curious() {
        let mut machine = StateMachine::new();
        let reaction = machine
            .handle(DesktopEvent::ActiveAppChanged {
                app_id: Some("org.kde.dolphin".to_string()),
            })
            .unwrap();
        assert_eq!(reaction.emotion, Emotion::Curious);
    }

    #[test]
    fn pause_suppresses_everything() {
        let mut machine = StateMachine::new();
        machine.set_paused(true);

        for event in [
            DesktopEvent::PetClicked,
            DesktopEvent::ActivityResumed,
            DesktopEvent::NotificationOccurred { category: None },
            DesktopEvent::PowerChanged {
                on_battery: true,
                percent: Some(1),
                state: PowerState::Discharging,
            },
        ] {
            assert_eq!(machine.handle(event), Err(Suppressed::Paused));
        }

        assert_eq!(machine.current_emotion(), Emotion::Idle);
    }

    #[test]
    fn state_settles_back_to_idle_after_ttl() {
        let mut machine = StateMachine::new();
        let base = Instant::now();

        machine.handle_at(DesktopEvent::PetClicked, base).unwrap();
        assert_eq!(machine.settle_at(at(base, 1)), None, "ttl ещё не истёк");
        assert_eq!(machine.settle_at(at(base, 3)), Some(Emotion::Idle));
        assert_eq!(machine.current_emotion(), Emotion::Idle);
    }

    #[test]
    fn app_id_does_not_change_the_outcome() {
        // Питомец замечает факт смены контекста, но не разбирает, какое
        // именно приложение активно: это было бы профилированием (§4.2).
        let mut base_machine = StateMachine::new();
        let first = base_machine
            .handle(DesktopEvent::ActiveAppChanged {
                app_id: Some("org.kde.konsole".to_string()),
            })
            .unwrap();

        let mut other_machine = StateMachine::new();
        let second = other_machine
            .handle(DesktopEvent::ActiveAppChanged {
                app_id: Some("com.example.banking".to_string()),
            })
            .unwrap();

        assert_eq!(first.emotion, second.emotion);
        assert_eq!(first.ttl_ms, second.ttl_ms);
    }
}
