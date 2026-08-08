//! Модель манифеста Pet Pack v1 (§FR-8).
//!
//! Разбор отделён от проверки: сначала JSON превращается в структуру,
//! потом [`super::validate`] решает, годится ли она. Так ошибка формата
//! и ошибка содержания различимы в сообщении пользователю.

use serde::Deserialize;

use std::collections::BTreeMap;

/// Единственная версия схемы, которую понимает MVP.
pub const SUPPORTED_SCHEMA_VERSION: u32 = 1;

/// Единственный способ отрисовки в v1.
///
/// Поле существует ради второго формата: спрайты не дают плавной анимации,
/// и векторный формат появится после MVP ([ADR-005](../../../docs/adr/0005-pet-pack-sprite-sheet.md)).
/// Без явного дискриминатора его добавление превратилось бы в угадывание
/// по расширению файла.
pub const RENDERER_SPRITE_SHEET: &str = "sprite-sheet";

#[derive(Debug, Clone, Deserialize)]
pub struct Grid {
    pub columns: u32,
    pub rows: u32,
    #[serde(rename = "cellWidth")]
    pub cell_width: u32,
    #[serde(rename = "cellHeight")]
    pub cell_height: u32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Animation {
    pub row: u32,
    /// Несколько состояний могут делить одну строку с разным началом:
    /// у встроенного питомца так сделаны `sleepy` и `low_battery`.
    #[serde(rename = "startColumn", default)]
    pub start_column: u32,
    pub frames: u32,
    #[serde(rename = "frameDurationMs")]
    pub frame_duration_ms: u32,
    /// Процедурное движение ([ADR-009](../../../docs/adr/0009-procedural-motion-layer.md)).
    ///
    /// Необязательное: его отсутствие означает тождественное преобразование,
    /// поэтому все существующие пакеты продолжают работать без изменений.
    #[serde(default)]
    pub motion: Option<Motion>,
}

/// Плавность перехода между ключевыми точками.
///
/// Набор закрыт схемой намеренно: Pet Pack остаётся данными, и произвольное
/// выражение здесь означало бы исполняемый код в пакете (§FR-8).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Easing {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
}

#[derive(Debug, Clone, Copy, PartialEq, Deserialize)]
pub struct Keyframe {
    /// Доля от длительности, 0.0..1.0.
    pub at: f64,
    /// Смещение в логических пикселях относительно покоя.
    #[serde(default)]
    pub x: f64,
    #[serde(default)]
    pub y: f64,
    /// Плавность подхода к **следующей** точке.
    #[serde(default = "default_easing")]
    pub easing: Easing,
}

fn default_easing() -> Easing {
    Easing::Linear
}

#[derive(Debug, Clone, PartialEq, Deserialize)]
pub struct Motion {
    #[serde(rename = "durationMs")]
    pub duration_ms: u32,
    #[serde(rename = "loop", default)]
    pub loop_: bool,
    pub keyframes: Vec<Keyframe>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    #[serde(rename = "schemaVersion")]
    pub schema_version: u32,
    pub renderer: String,
    pub id: String,
    pub name: String,
    pub version: String,
    pub sheet: String,
    /// Контрольная сумма листа, шестнадцатеричная строка SHA-256.
    ///
    /// Необязательная: пакеты без неё остаются валидными. Но если она
    /// объявлена, она обязана совпадать — объявленная и непроверяемая
    /// сумма хуже отсутствующей, потому что выглядит гарантией.
    #[serde(rename = "sheetSha256", default)]
    pub sheet_sha256: Option<String>,
    pub grid: Grid,
    /// BTreeMap, а не HashMap: порядок обхода детерминирован, и сообщения
    /// валидатора не меняются от запуска к запуску.
    pub animations: BTreeMap<String, Animation>,
    #[serde(rename = "fallbackAnimation")]
    pub fallback_animation: String,
    #[serde(default)]
    pub locales: Vec<String>,
}

impl Manifest {
    /// Разбирает манифест. Ошибки разбора не содержат содержимого файла:
    /// в сообщение попадает позиция и вид ошибки, но не сам текст, который
    /// может оказаться чем угодно.
    pub fn parse(bytes: &[u8]) -> Result<Manifest, ManifestError> {
        // Ограничение до разбора, а не после: манифест на сотню мегабайт
        // не должен доходить до парсера вообще.
        if bytes.len() > MAX_MANIFEST_BYTES {
            return Err(ManifestError::TooLarge {
                bytes: bytes.len(),
                limit: MAX_MANIFEST_BYTES,
            });
        }

        serde_json::from_slice(bytes).map_err(|error| ManifestError::Malformed {
            line: error.line(),
            column: error.column(),
        })
    }

    /// Число объявленных кадров во всех анимациях.
    pub fn declared_frames(&self) -> u64 {
        self.animations.values().map(|a| u64::from(a.frames)).sum()
    }
}

/// Манифест длиннее этого не бывает: 64 КиБ — это тысячи анимаций.
pub const MAX_MANIFEST_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ManifestError {
    TooLarge { bytes: usize, limit: usize },
    Malformed { line: usize, column: usize },
}

impl std::fmt::Display for ManifestError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ManifestError::TooLarge { bytes, limit } => {
                write!(
                    f,
                    "manifest.json слишком большой: {bytes} байт при пределе {limit}"
                )
            }
            ManifestError::Malformed { line, column } => {
                write!(
                    f,
                    "manifest.json не разбирается: строка {line}, позиция {column}"
                )
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MINIMAL: &str = r#"{
        "schemaVersion": 1,
        "renderer": "sprite-sheet",
        "id": "org.example.pet",
        "name": "Example",
        "version": "1.0.0",
        "sheet": "sheet.png",
        "grid": { "columns": 8, "rows": 11, "cellWidth": 192, "cellHeight": 208 },
        "animations": {
            "idle": { "row": 0, "frames": 6, "frameDurationMs": 220 }
        },
        "fallbackAnimation": "idle",
        "locales": ["ru", "en"]
    }"#;

    #[test]
    fn parses_minimal_manifest() {
        let manifest = Manifest::parse(MINIMAL.as_bytes()).expect("минимальный манифест");
        assert_eq!(manifest.schema_version, SUPPORTED_SCHEMA_VERSION);
        assert_eq!(manifest.renderer, RENDERER_SPRITE_SHEET);
        assert_eq!(manifest.animations["idle"].frames, 6);
        // startColumn необязателен и по умолчанию нулевой.
        assert_eq!(manifest.animations["idle"].start_column, 0);
        assert_eq!(manifest.declared_frames(), 6);
    }

    #[test]
    fn rejects_oversized_manifest() {
        let huge = vec![b' '; MAX_MANIFEST_BYTES + 1];
        assert!(matches!(
            Manifest::parse(&huge),
            Err(ManifestError::TooLarge { .. })
        ));
    }

    #[test]
    fn rejects_malformed_json_without_leaking_content() {
        let broken = r#"{"schemaVersion": 1, "секрет": "не должен попасть в ошибку""#;
        let error = Manifest::parse(broken.as_bytes()).unwrap_err();
        let message = error.to_string();
        assert!(
            !message.contains("секрет"),
            "ошибка не должна цитировать файл: {message}"
        );
    }

    #[test]
    fn rejects_missing_required_fields() {
        // Без renderer манифест не принимается: дискриминатор обязателен
        // уже в v1, хотя значение пока одно.
        let without_renderer = MINIMAL.replace(r#""renderer": "sprite-sheet","#, "");
        assert!(Manifest::parse(without_renderer.as_bytes()).is_err());
    }

    #[test]
    fn survives_deeply_nested_input() {
        // Глубокая вложенность — классический способ уронить разборщик.
        // Ожидаем ошибку, а не панику и не переполнение стека.
        let deep = format!("{}{}", "[".repeat(2000), "]".repeat(2000));
        assert!(Manifest::parse(deep.as_bytes()).is_err());
    }

    #[test]
    fn rejects_duplicate_keys() {
        let duplicated = MINIMAL.replace(
            r#""fallbackAnimation": "idle","#,
            r#""fallbackAnimation": "idle", "fallbackAnimation": "happy","#,
        );

        // Повторяющийся ключ отвергается, и это правильное поведение
        // для недоверенного ввода. Разные разборщики решают такой конфликт
        // по-разному — кто-то берёт первое значение, кто-то последнее.
        // Пакет, который наш валидатор и наш загрузчик прочитают одинаково,
        // не должен зависеть от этого выбора; отказ снимает вопрос.
        assert!(
            Manifest::parse(duplicated.as_bytes()).is_err(),
            "манифест с повторяющимся ключом должен отвергаться"
        );
    }
}
