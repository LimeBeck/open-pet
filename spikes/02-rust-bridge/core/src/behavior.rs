//! Срез поведения из §FR-5: приоритеты, cooldown, детерминированность.
//!
//! Здесь нет ничего от FFI и тем более от Qt — это и есть проверяемое
//! утверждение ADR-001: домен не знает, кто его вызывает.

use std::collections::HashMap;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Emotion {
    Idle,
    Happy,
    Curious,
    Sleepy,
    Charging,
    LowBattery,
    Notification,
    Busy,
}

impl Emotion {
    /// Порядок из §FR-5: low_battery → notification → charging →
    /// happy/curious → busy → sleepy → idle.
    pub fn priority(self) -> u8 {
        match self {
            Emotion::LowBattery => 70,
            Emotion::Notification => 60,
            Emotion::Charging => 50,
            Emotion::Happy | Emotion::Curious => 40,
            Emotion::Busy => 30,
            Emotion::Sleepy => 20,
            Emotion::Idle => 10,
        }
    }
}

#[derive(Debug, Clone)]
pub enum DesktopEvent {
    ActivityResumed,
    IdleThresholdReached { seconds: u32 },
    PowerChanged { on_battery: bool, percent: Option<u8> },
    ActiveAppChanged { app_id: Option<String> },
    PetClicked,
}

#[derive(Debug, Clone)]
pub struct Reaction {
    pub emotion: Emotion,
    pub priority: u8,
    pub ttl_ms: u32,
    pub cooldown_key: Option<String>,
}

pub struct StateMachine {
    current: Emotion,
    current_until: Option<Instant>,
    cooldowns: HashMap<String, Instant>,
    low_battery_threshold: u8,
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
            low_battery_threshold: 20,
        }
    }

    pub fn handle(&mut self, event: DesktopEvent) -> Option<Reaction> {
        self.handle_at(event, Instant::now())
    }

    /// Время передаётся снаружи, чтобы правила тестировались без ожидания.
    pub fn handle_at(&mut self, event: DesktopEvent, now: Instant) -> Option<Reaction> {
        let candidate = self.candidate_for(event)?;

        if let Some(key) = &candidate.cooldown_key {
            if let Some(until) = self.cooldowns.get(key) {
                if now < *until {
                    return None;
                }
            }
        }

        // Более приоритетное состояние прерывает менее приоритетное;
        // равное или меньшее ждёт истечения ttl текущего.
        let current_active = self.current_until.map(|until| now < until).unwrap_or(false);
        if current_active && candidate.priority <= self.current.priority() {
            return None;
        }

        self.current = candidate.emotion;
        self.current_until = Some(now + Duration::from_millis(u64::from(candidate.ttl_ms)));

        if let Some(key) = &candidate.cooldown_key {
            self.cooldowns
                .insert(key.clone(), now + Duration::from_secs(30));
        }

        Some(candidate)
    }

    fn candidate_for(&self, event: DesktopEvent) -> Option<Reaction> {
        let reaction = match event {
            DesktopEvent::ActivityResumed => Reaction {
                emotion: Emotion::Happy,
                priority: Emotion::Happy.priority(),
                ttl_ms: 4000,
                cooldown_key: Some("activity_resumed".to_string()),
            },
            DesktopEvent::IdleThresholdReached { .. } => Reaction {
                emotion: Emotion::Sleepy,
                priority: Emotion::Sleepy.priority(),
                ttl_ms: 60_000,
                cooldown_key: None,
            },
            DesktopEvent::PowerChanged { on_battery, percent } => {
                let low = percent
                    .map(|value| value <= self.low_battery_threshold)
                    .unwrap_or(false);

                if on_battery && low {
                    Reaction {
                        emotion: Emotion::LowBattery,
                        priority: Emotion::LowBattery.priority(),
                        ttl_ms: 8000,
                        cooldown_key: Some("low_battery".to_string()),
                    }
                } else if !on_battery {
                    Reaction {
                        emotion: Emotion::Charging,
                        priority: Emotion::Charging.priority(),
                        ttl_ms: 5000,
                        cooldown_key: Some("charging".to_string()),
                    }
                } else {
                    return None;
                }
            }
            DesktopEvent::ActiveAppChanged { app_id } => Reaction {
                emotion: if app_id.is_some() {
                    Emotion::Curious
                } else {
                    Emotion::Idle
                },
                priority: Emotion::Curious.priority(),
                ttl_ms: 3000,
                cooldown_key: Some("active_app".to_string()),
            },
            DesktopEvent::PetClicked => Reaction {
                emotion: Emotion::Happy,
                priority: Emotion::Happy.priority(),
                ttl_ms: 2500,
                cooldown_key: Some("pet_clicked".to_string()),
            },
        };

        Some(reaction)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn low_battery_interrupts_sleepy() {
        let mut machine = StateMachine::new();
        let now = Instant::now();

        let sleepy = machine
            .handle_at(DesktopEvent::IdleThresholdReached { seconds: 300 }, now)
            .expect("простой даёт реакцию");
        assert_eq!(sleepy.emotion, Emotion::Sleepy);

        let low = machine
            .handle_at(
                DesktopEvent::PowerChanged {
                    on_battery: true,
                    percent: Some(9),
                },
                now,
            )
            .expect("низкий заряд важнее сна");
        assert_eq!(low.emotion, Emotion::LowBattery);
    }

    #[test]
    fn cooldown_suppresses_repeat() {
        let mut machine = StateMachine::new();
        let now = Instant::now();

        assert!(machine.handle_at(DesktopEvent::PetClicked, now).is_some());
        let later = now + Duration::from_secs(1);
        assert!(machine.handle_at(DesktopEvent::PetClicked, later).is_none());

        let after_cooldown = now + Duration::from_secs(31);
        assert!(machine
            .handle_at(DesktopEvent::PetClicked, after_cooldown)
            .is_some());
    }

    #[test]
    fn discharging_without_low_percent_is_silent() {
        let mut machine = StateMachine::new();
        assert!(machine
            .handle(DesktopEvent::PowerChanged {
                on_battery: true,
                percent: Some(80),
            })
            .is_none());
    }
}
