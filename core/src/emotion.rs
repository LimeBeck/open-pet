//! Эмоции и их приоритеты (§4.1, §FR-5).

/// Восемь состояний MVP. Расширение списка — вопрос отдельного ADR: каждое
/// состояние обязано иметь анимацию или корректный fallback (§13, п. 2).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
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
    /// Порядок из §FR-5:
    /// `low_battery` → `notification` → `charging` → `happy`/`curious`
    /// → `busy` → `sleepy` → `idle`.
    ///
    /// Числа расставлены с промежутками: вставить состояние между соседними
    /// не должно требовать перенумерации остальных.
    pub const fn priority(self) -> u8 {
        match self {
            Emotion::LowBattery => 70,
            Emotion::Notification => 60,
            Emotion::Charging => 50,
            Emotion::Happy => 40,
            Emotion::Curious => 40,
            Emotion::Busy => 30,
            Emotion::Sleepy => 20,
            Emotion::Idle => 10,
        }
    }

    /// Устойчивое имя для журналов и для сопоставления с анимацией Pet Pack.
    pub const fn name(self) -> &'static str {
        match self {
            Emotion::Idle => "idle",
            Emotion::Happy => "happy",
            Emotion::Curious => "curious",
            Emotion::Sleepy => "sleepy",
            Emotion::Charging => "charging",
            Emotion::LowBattery => "low_battery",
            Emotion::Notification => "notification",
            Emotion::Busy => "busy",
        }
    }

    pub const ALL: [Emotion; 8] = [
        Emotion::Idle,
        Emotion::Happy,
        Emotion::Curious,
        Emotion::Sleepy,
        Emotion::Charging,
        Emotion::LowBattery,
        Emotion::Notification,
        Emotion::Busy,
    ];
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn priority_order_matches_spec() {
        // Порядок §FR-5 записан здесь как проверяемое утверждение,
        // а не как комментарий рядом с числами.
        let expected = [
            Emotion::LowBattery,
            Emotion::Notification,
            Emotion::Charging,
            Emotion::Happy,
            Emotion::Busy,
            Emotion::Sleepy,
            Emotion::Idle,
        ];

        for pair in expected.windows(2) {
            assert!(
                pair[0].priority() > pair[1].priority(),
                "{} должен быть приоритетнее {}",
                pair[0].name(),
                pair[1].name()
            );
        }

        assert_eq!(Emotion::Happy.priority(), Emotion::Curious.priority());
    }

    #[test]
    fn names_are_unique() {
        let mut names: Vec<&str> = Emotion::ALL.iter().map(|e| e.name()).collect();
        names.sort_unstable();
        let count = names.len();
        names.dedup();
        assert_eq!(names.len(), count, "имена эмоций должны быть уникальны");
    }
}
