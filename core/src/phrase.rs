//! Шаблонные реплики (§FR-6).
//!
//! Реплики выбираются по намерению, а не по тексту события: правило говорит
//! «поприветствуй», а не «скажи вот эту строку». Это нужно и для локализации,
//! и для будущей LLM (M6) — ей уходит намерение и локаль, но не событие (§US-06).
//!
//! Хранится и сравнивается **идентификатор** показанной фразы, не её текст:
//! §9 разрешает держать короткую историю идентификаторов и ничего сверх того.

use std::collections::VecDeque;

/// Сколько последних реплик помнить, чтобы не повторяться (§FR-6).
const DEFAULT_HISTORY: usize = 12;

/// Максимальная длина реплики в байтах UTF-8. Ограничение существует
/// не ради экономии: короткая реплика — продуктовое требование (§FR-6),
/// и то же ограничение позже придётся навязывать ответу LLM.
pub const MAX_PHRASE_BYTES: usize = 180;

/// Намерение реплики. Общее для шаблонов и для будущего LLM-провайдера.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PhraseIntent {
    /// Пользователь вернулся к компьютеру.
    WelcomeBack,
    /// Начинается простой, питомец засыпает.
    GettingSleepy,
    /// Подключили питание.
    Charging,
    /// Заряд ниже порога.
    LowBattery,
    /// Пришло уведомление.
    NoticedNotification,
    /// Сменилось активное приложение.
    NoticedContext,
    /// Начали воспроизводить медиа.
    MediaStarted,
    /// По питомцу кликнули.
    Petted,
}

impl PhraseIntent {
    pub const fn name(self) -> &'static str {
        match self {
            PhraseIntent::WelcomeBack => "welcome_back",
            PhraseIntent::GettingSleepy => "getting_sleepy",
            PhraseIntent::Charging => "charging",
            PhraseIntent::LowBattery => "low_battery",
            PhraseIntent::NoticedNotification => "noticed_notification",
            PhraseIntent::NoticedContext => "noticed_context",
            PhraseIntent::MediaStarted => "media_started",
            PhraseIntent::Petted => "petted",
        }
    }

    pub const ALL: [PhraseIntent; 8] = [
        PhraseIntent::WelcomeBack,
        PhraseIntent::GettingSleepy,
        PhraseIntent::Charging,
        PhraseIntent::LowBattery,
        PhraseIntent::NoticedNotification,
        PhraseIntent::NoticedContext,
        PhraseIntent::MediaStarted,
        PhraseIntent::Petted,
    ];
}

/// Локаль реплик. §7 требует ru и en с обязательным fallback.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Locale {
    Ru,
    En,
}

impl Locale {
    /// Разбирает тег вида `ru`, `ru_RU.UTF-8`, `en-GB`. Всё неизвестное
    /// становится `En`: fallback обязан быть определён всегда (§7).
    pub fn parse(tag: &str) -> Locale {
        let primary = tag
            .split(['_', '-', '.'])
            .next()
            .unwrap_or("")
            .to_ascii_lowercase();

        match primary.as_str() {
            "ru" => Locale::Ru,
            _ => Locale::En,
        }
    }

    pub const fn name(self) -> &'static str {
        match self {
            Locale::Ru => "ru",
            Locale::En => "en",
        }
    }
}

/// Выбранная реплика: текст для показа и идентификатор для истории.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Phrase {
    pub id: &'static str,
    pub text: &'static str,
}

struct Template {
    id: &'static str,
    intent: PhraseIntent,
    ru: &'static str,
    en: &'static str,
}

/// Каталог реплик.
///
/// Тексты намеренно короткие и без обращения к содержимому событий: питомец
/// не знает, какое пришло уведомление и в каком приложении работает
/// пользователь, и сказать об этом не может (§4.2).
const TEMPLATES: &[Template] = &[
    Template {
        id: "welcome_back.1",
        intent: PhraseIntent::WelcomeBack,
        ru: "С возвращением!",
        en: "Welcome back!",
    },
    Template {
        id: "welcome_back.2",
        intent: PhraseIntent::WelcomeBack,
        ru: "О, ты здесь.",
        en: "Oh, you're here.",
    },
    Template {
        id: "welcome_back.3",
        intent: PhraseIntent::WelcomeBack,
        ru: "Я подождал.",
        en: "I waited for you.",
    },
    Template {
        id: "getting_sleepy.1",
        intent: PhraseIntent::GettingSleepy,
        ru: "Что-то в сон клонит…",
        en: "Getting sleepy…",
    },
    Template {
        id: "getting_sleepy.2",
        intent: PhraseIntent::GettingSleepy,
        ru: "Подремлю, пока тихо.",
        en: "I'll nap while it's quiet.",
    },
    Template {
        id: "charging.1",
        intent: PhraseIntent::Charging,
        ru: "Заряжаемся!",
        en: "Charging up!",
    },
    Template {
        id: "charging.2",
        intent: PhraseIntent::Charging,
        ru: "Так-то лучше.",
        en: "That's better.",
    },
    Template {
        id: "low_battery.1",
        intent: PhraseIntent::LowBattery,
        ru: "Заряд на исходе.",
        en: "Battery is running low.",
    },
    Template {
        id: "low_battery.2",
        intent: PhraseIntent::LowBattery,
        ru: "Может, к розетке?",
        en: "Maybe find a socket?",
    },
    Template {
        id: "noticed_notification.1",
        intent: PhraseIntent::NoticedNotification,
        ru: "Кажется, тебе написали.",
        en: "Something came in.",
    },
    Template {
        id: "noticed_notification.2",
        intent: PhraseIntent::NoticedNotification,
        ru: "Там что-то звякнуло.",
        en: "I heard a ping.",
    },
    Template {
        id: "noticed_context.1",
        intent: PhraseIntent::NoticedContext,
        ru: "Новое занятие?",
        en: "Something new?",
    },
    Template {
        id: "noticed_context.2",
        intent: PhraseIntent::NoticedContext,
        ru: "Переключились.",
        en: "We moved on.",
    },
    Template {
        id: "media_started.1",
        intent: PhraseIntent::MediaStarted,
        ru: "Люблю эту.",
        en: "I like this one.",
    },
    Template {
        id: "media_started.2",
        intent: PhraseIntent::MediaStarted,
        ru: "Не буду мешать.",
        en: "I'll keep quiet then.",
    },
    Template {
        id: "petted.1",
        intent: PhraseIntent::Petted,
        ru: "Ещё разок!",
        en: "Do that again!",
    },
    Template {
        id: "petted.2",
        intent: PhraseIntent::Petted,
        ru: "Приятно.",
        en: "That's nice.",
    },
    Template {
        id: "petted.3",
        intent: PhraseIntent::Petted,
        ru: "Мурр.",
        en: "Mrrp.",
    },
];

pub struct PhraseBook {
    locale: Locale,
    /// Идентификаторы показанного, новейшие в начале. Текст не хранится (§9).
    history: VecDeque<&'static str>,
    history_limit: usize,
    /// Счётчик обращений: даёт разнообразие без генератора случайных чисел
    /// и, что важнее, делает выбор воспроизводимым в тестах.
    cursor: usize,
}

impl Default for PhraseBook {
    fn default() -> Self {
        Self::new(Locale::En)
    }
}

impl PhraseBook {
    pub fn new(locale: Locale) -> Self {
        Self {
            locale,
            history: VecDeque::new(),
            history_limit: DEFAULT_HISTORY,
            cursor: 0,
        }
    }

    pub fn locale(&self) -> Locale {
        self.locale
    }

    pub fn set_locale(&mut self, locale: Locale) {
        if self.locale != locale {
            self.locale = locale;
            // История хранит идентификаторы, а не тексты, поэтому при смене
            // языка она остаётся осмысленной и не сбрасывается.
        }
    }

    pub fn set_history_limit(&mut self, limit: usize) {
        self.history_limit = limit;
        self.trim();
    }

    /// Выбирает реплику для намерения, избегая недавно показанных.
    ///
    /// Если все варианты намерения уже в истории, повтор разрешается:
    /// промолчать хуже, чем повториться, а намерений с одним вариантом
    /// быть не должно — за этим следит тест.
    pub fn pick(&mut self, intent: PhraseIntent) -> Option<Phrase> {
        let candidates: Vec<&Template> = TEMPLATES.iter().filter(|t| t.intent == intent).collect();

        if candidates.is_empty() {
            return None;
        }

        let fresh: Vec<&&Template> = candidates
            .iter()
            .filter(|t| !self.history.contains(&t.id))
            .collect();

        let chosen = if fresh.is_empty() {
            candidates[self.cursor % candidates.len()]
        } else {
            fresh[self.cursor % fresh.len()]
        };

        self.cursor = self.cursor.wrapping_add(1);
        self.remember(chosen.id);

        Some(Phrase {
            id: chosen.id,
            text: match self.locale {
                Locale::Ru => chosen.ru,
                Locale::En => chosen.en,
            },
        })
    }

    fn remember(&mut self, id: &'static str) {
        self.history.push_front(id);
        self.trim();
    }

    fn trim(&mut self) {
        while self.history.len() > self.history_limit {
            self.history.pop_back();
        }
    }

    /// Сброс локальных данных (§9): история показанных реплик стирается.
    pub fn clear_history(&mut self) {
        self.history.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_intent_has_at_least_two_variants() {
        // Одного варианта мало: защита от повторов из §FR-6 не сможет
        // ничего выбрать и питомец начнёт заедать.
        for intent in PhraseIntent::ALL {
            let count = TEMPLATES.iter().filter(|t| t.intent == intent).count();
            assert!(
                count >= 2,
                "у намерения {} только {count} вариант(ов)",
                intent.name()
            );
        }
    }

    #[test]
    fn every_template_has_both_locales_and_fits_the_limit() {
        for t in TEMPLATES {
            assert!(!t.ru.is_empty(), "{} без русского текста", t.id);
            assert!(!t.en.is_empty(), "{} без английского текста", t.id);
            assert!(
                t.ru.len() <= MAX_PHRASE_BYTES && t.en.len() <= MAX_PHRASE_BYTES,
                "{} длиннее {MAX_PHRASE_BYTES} байт",
                t.id
            );
        }
    }

    #[test]
    fn template_ids_are_unique() {
        let mut ids: Vec<&str> = TEMPLATES.iter().map(|t| t.id).collect();
        ids.sort_unstable();
        let total = ids.len();
        ids.dedup();
        assert_eq!(
            ids.len(),
            total,
            "идентификаторы реплик должны быть уникальны"
        );
    }

    #[test]
    fn does_not_repeat_within_history() {
        let mut book = PhraseBook::new(Locale::Ru);
        let variants = TEMPLATES
            .iter()
            .filter(|t| t.intent == PhraseIntent::Petted)
            .count();

        let mut seen = Vec::new();
        for _ in 0..variants {
            seen.push(book.pick(PhraseIntent::Petted).unwrap().id);
        }

        let mut unique = seen.clone();
        unique.sort_unstable();
        unique.dedup();
        assert_eq!(
            unique.len(),
            variants,
            "варианты не должны повторяться: {seen:?}"
        );
    }

    #[test]
    fn repeats_only_after_exhausting_variants() {
        let mut book = PhraseBook::new(Locale::En);
        let mut ids = Vec::new();
        for _ in 0..10 {
            ids.push(book.pick(PhraseIntent::Charging).unwrap().id);
        }
        // Вариантов у charging два, значит на десяти показах повторы неизбежны,
        // но два подряд одинаковых — нет.
        for pair in ids.windows(2) {
            assert_ne!(pair[0], pair[1], "две одинаковые реплики подряд: {ids:?}");
        }
    }

    #[test]
    fn locale_switch_changes_text_but_not_identity() {
        let mut ru = PhraseBook::new(Locale::Ru);
        let mut en = PhraseBook::new(Locale::En);

        let a = ru.pick(PhraseIntent::WelcomeBack).unwrap();
        let b = en.pick(PhraseIntent::WelcomeBack).unwrap();

        assert_eq!(a.id, b.id);
        assert_ne!(a.text, b.text);
    }

    #[test]
    fn unknown_locale_falls_back_to_english() {
        assert_eq!(Locale::parse("de_DE.UTF-8"), Locale::En);
        assert_eq!(Locale::parse(""), Locale::En);
        assert_eq!(Locale::parse("ru_RU.UTF-8"), Locale::Ru);
        assert_eq!(Locale::parse("RU"), Locale::Ru);
        assert_eq!(Locale::parse("en-GB"), Locale::En);
    }

    #[test]
    fn history_is_bounded() {
        let mut book = PhraseBook::new(Locale::En);
        book.set_history_limit(3);
        for _ in 0..50 {
            book.pick(PhraseIntent::Petted);
        }
        assert!(book.history.len() <= 3);
    }

    #[test]
    fn clearing_history_forgets_everything() {
        let mut book = PhraseBook::new(Locale::Ru);
        book.pick(PhraseIntent::Petted);
        book.clear_history();
        assert!(book.history.is_empty());
    }
}
