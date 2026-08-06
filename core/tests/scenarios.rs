//! Сценарные тесты: последовательности событий из §5 спецификации.
//!
//! Модульные тесты в `behavior.rs` проверяют отдельные правила, эти —
//! поведение на связных историях, которые описаны как пользовательские
//! сценарии.

use openpet_core::{DesktopEvent, Emotion, MediaState, PowerState, SessionState, StateMachine};

use std::time::{Duration, Instant};

fn at(base: Instant, secs: u64) -> Instant {
    base + Duration::from_secs(secs)
}

/// US-03: простой и возвращение.
#[test]
fn us03_idle_then_return() {
    let mut machine = StateMachine::new();
    let base = Instant::now();

    machine
        .handle_at(DesktopEvent::IdleThresholdReached { seconds: 300 }, base)
        .expect("простой усыпляет питомца");
    assert_eq!(machine.current_emotion(), Emotion::Sleepy);

    let woken = machine
        .handle_at(DesktopEvent::ActivityResumed, at(base, 600))
        .expect("возвращение будит питомца");
    assert_eq!(woken.emotion, Emotion::Happy);

    // Не чаще одного раза за cooldown (§US-03).
    assert!(machine
        .handle_at(DesktopEvent::ActivityResumed, at(base, 605))
        .is_err());
}

/// US-04: питание. Повторные обновления одного состояния не создают
/// новые реплики.
#[test]
fn us04_power_transitions_do_not_repeat() {
    let mut machine = StateMachine::new();
    let base = Instant::now();

    let charging = machine
        .handle_at(
            DesktopEvent::PowerChanged {
                on_battery: false,
                percent: Some(55),
                state: PowerState::Charging,
            },
            base,
        )
        .expect("подключение питания замечено");
    assert_eq!(charging.emotion, Emotion::Charging);

    // UPower шлёт обновления процента постоянно — ни одно из них
    // не должно порождать новую реакцию.
    for second in 1..10 {
        assert!(
            machine
                .handle_at(
                    DesktopEvent::PowerChanged {
                        on_battery: false,
                        percent: Some(55 + second as u8),
                        state: PowerState::Charging,
                    },
                    at(base, second),
                )
                .is_err(),
            "обновление заряда на {second}-й секунде не должно давать реакцию"
        );
    }
}

/// US-05: смена контекста приложения.
#[test]
fn us05_app_switch_is_noticed_once_per_cooldown() {
    let mut machine = StateMachine::new();
    let base = Instant::now();

    assert!(machine
        .handle_at(
            DesktopEvent::ActiveAppChanged {
                app_id: Some("org.kde.konsole".to_string())
            },
            base,
        )
        .is_ok());

    // Быстрый alt-tab не должен превращаться в поток реакций (§10).
    for second in 1..20 {
        assert!(machine
            .handle_at(
                DesktopEvent::ActiveAppChanged {
                    app_id: Some(format!("app.number{second}"))
                },
                at(base, second),
            )
            .is_err());
    }
}

/// Сессия: блокировка усыпляет, возобновление будит.
#[test]
fn session_lock_and_resume() {
    let mut machine = StateMachine::new();
    let base = Instant::now();

    machine
        .handle_at(
            DesktopEvent::SessionChanged {
                state: SessionState::Locked,
            },
            base,
        )
        .expect("блокировка усыпляет");
    assert_eq!(machine.current_emotion(), Emotion::Sleepy);

    let resumed = machine
        .handle_at(
            DesktopEvent::SessionChanged {
                state: SessionState::Resumed,
            },
            at(base, 60),
        )
        .expect("возобновление будит");
    assert_eq!(resumed.emotion, Emotion::Happy);
}

/// Все восемь состояний достижимы событиями. Если состояние недостижимо,
/// анимация для него в Pet Pack никогда не покажется (§13, п. 2).
#[test]
fn every_emotion_is_reachable() {
    let base = Instant::now();

    let cases: Vec<(Emotion, Vec<DesktopEvent>)> = vec![
        (Emotion::Happy, vec![DesktopEvent::PetClicked]),
        (
            Emotion::Sleepy,
            vec![DesktopEvent::IdleThresholdReached { seconds: 300 }],
        ),
        (
            Emotion::Charging,
            vec![DesktopEvent::PowerChanged {
                on_battery: false,
                percent: Some(50),
                state: PowerState::Charging,
            }],
        ),
        (
            Emotion::LowBattery,
            vec![DesktopEvent::PowerChanged {
                on_battery: true,
                percent: Some(5),
                state: PowerState::Discharging,
            }],
        ),
        (
            Emotion::Notification,
            vec![DesktopEvent::NotificationOccurred {
                category: Some("im".to_string()),
            }],
        ),
        (
            Emotion::Busy,
            vec![DesktopEvent::MediaChanged {
                state: MediaState::Playing,
            }],
        ),
        (
            Emotion::Curious,
            vec![DesktopEvent::ActiveAppChanged {
                app_id: Some("org.kde.dolphin".to_string()),
            }],
        ),
    ];

    for (expected, events) in cases {
        let mut machine = StateMachine::new();
        let mut reached = None;
        for event in events {
            reached = machine.handle_at(event, base).ok().map(|r| r.emotion);
        }
        assert_eq!(
            reached,
            Some(expected),
            "состояние {} должно достигаться событиями",
            expected.name()
        );
    }

    // Idle — состояние покоя: в него возвращаются по истечении ttl.
    let mut machine = StateMachine::new();
    machine.handle_at(DesktopEvent::PetClicked, base).unwrap();
    assert_eq!(machine.settle_at(at(base, 5)), Some(Emotion::Idle));
}

/// Никакая последовательность событий не должна оставлять питомца
/// в состоянии навсегда: ttl всегда приводит его в покой.
#[test]
fn no_state_sticks_forever() {
    let base = Instant::now();

    for emotion_event in [
        DesktopEvent::PetClicked,
        DesktopEvent::ActivityResumed,
        DesktopEvent::NotificationOccurred { category: None },
        DesktopEvent::MediaChanged {
            state: MediaState::Playing,
        },
        DesktopEvent::PowerChanged {
            on_battery: true,
            percent: Some(3),
            state: PowerState::Discharging,
        },
    ] {
        let mut machine = StateMachine::new();
        machine.handle_at(emotion_event.clone(), base).unwrap();

        // Час — заведомо больше любого ttl, кроме сна, который и должен
        // держаться до следующего события.
        assert_eq!(
            machine.settle_at(at(base, 3600)),
            Some(Emotion::Idle),
            "состояние после {emotion_event:?} должно истечь"
        );
    }
}
