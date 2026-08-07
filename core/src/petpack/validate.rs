//! Проверка Pet Pack (§FR-8, [ADR-005](../../../docs/adr/0005-pet-pack-sprite-sheet.md)).
//!
//! Возвращается не «да/нет», а отчёт: ошибки отклоняют пакет, предупреждения
//! его пропускают. §US-07 требует «понятного списка ошибок», поэтому проверка
//! не останавливается на первой — иначе пользователь чинит пакет по одной
//! проблеме за раз.

use super::manifest::{Manifest, RENDERER_SPRITE_SHEET, SUPPORTED_SCHEMA_VERSION};

/// Пределы из §FR-8 и [ADR-005](../../../docs/adr/0005-pet-pack-sprite-sheet.md).
///
/// Вынесены в структуру, а не в константы: тесты задают свои значения,
/// а настройки диагностики смогут показать действующие.
#[derive(Debug, Clone, Copy)]
pub struct Limits {
    /// Предел размерности листа. Про совместимость со слабыми GPU,
    /// а не про память: пакет, собранный на хорошем железе, должен
    /// открываться и на плохом.
    pub max_sheet_dimension: u32,
    /// Предел памяти под текстуру в байтах RGBA. Текстура занимает память
    /// целиком, сколько бы кадров ни было объявлено.
    pub max_texture_bytes: u64,
    /// Минимальная доля ячеек сетки, покрытых объявленными кадрами.
    /// Ловит «огромный лист, пара кадров» там, где предел памяти соблюдён.
    pub min_declared_coverage: f64,
    pub max_frames_per_animation: u32,
    pub min_frame_duration_ms: u32,
    pub max_frame_duration_ms: u32,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            max_sheet_dimension: 8192,
            max_texture_bytes: 16 * 1024 * 1024,
            min_declared_coverage: 0.40,
            max_frames_per_animation: 256,
            min_frame_duration_ms: 20,
            max_frame_duration_ms: 10_000,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Severity {
    /// Пакет отклоняется.
    Error,
    /// Пакет принимается, но что-то в нём сделано плохо.
    Warning,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Finding {
    pub severity: Severity,
    pub message: String,
}

impl Finding {
    fn error(message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Error,
            message: message.into(),
        }
    }

    fn warning(message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Warning,
            message: message.into(),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Report {
    pub findings: Vec<Finding>,
}

impl Report {
    pub fn is_acceptable(&self) -> bool {
        !self.findings.iter().any(|f| f.severity == Severity::Error)
    }

    pub fn errors(&self) -> impl Iterator<Item = &Finding> {
        self.findings
            .iter()
            .filter(|f| f.severity == Severity::Error)
    }

    pub fn warnings(&self) -> impl Iterator<Item = &Finding> {
        self.findings
            .iter()
            .filter(|f| f.severity == Severity::Warning)
    }
}

/// Восемь состояний §4.1. Отсутствующие подменяются `fallbackAnimation`,
/// но об этом стоит предупредить: питомец с одной анимацией на всё
/// формально корректен и совершенно неинтересен.
const REQUIRED_STATES: [&str; 8] = [
    "idle",
    "happy",
    "curious",
    "sleepy",
    "charging",
    "low_battery",
    "notification",
    "busy",
];

/// Проверяет манифест и фактические размеры листа.
///
/// Размеры передаются снаружи: декодирование изображения — забота хоста,
/// у ядра нет и не должно быть декодера картинок. Ядро проверяет числа
/// и их согласованность.
pub fn validate(
    manifest: &Manifest,
    sheet_width: u32,
    sheet_height: u32,
    limits: &Limits,
) -> Report {
    let mut report = Report::default();

    check_versions(manifest, &mut report);
    check_grid(manifest, sheet_width, sheet_height, limits, &mut report);
    check_animations(manifest, &mut report, limits);
    check_coverage(manifest, limits, &mut report);
    check_states(manifest, &mut report);

    report
}

fn check_versions(manifest: &Manifest, report: &mut Report) {
    if manifest.schema_version != SUPPORTED_SCHEMA_VERSION {
        // Отказ внятный: пакет из будущего должен получать «не поддерживается
        // этой версией», а не невнятный сбой при разборе.
        report.findings.push(Finding::error(format!(
            "schemaVersion {} не поддерживается, эта версия понимает только {}",
            manifest.schema_version, SUPPORTED_SCHEMA_VERSION
        )));
    }

    if manifest.renderer != RENDERER_SPRITE_SHEET {
        report.findings.push(Finding::error(format!(
            "renderer «{}» не поддерживается, эта версия умеет только «{}»",
            manifest.renderer, RENDERER_SPRITE_SHEET
        )));
    }

    if manifest.id.trim().is_empty() {
        report.findings.push(Finding::error("id пуст"));
    }

    if manifest.sheet.trim().is_empty() {
        report.findings.push(Finding::error("не указан файл листа"));
    }
}

fn check_grid(
    manifest: &Manifest,
    sheet_width: u32,
    sheet_height: u32,
    limits: &Limits,
    report: &mut Report,
) {
    let grid = &manifest.grid;

    if grid.columns == 0 || grid.rows == 0 || grid.cell_width == 0 || grid.cell_height == 0 {
        report
            .findings
            .push(Finding::error("сетка задана нулевыми размерами"));
        return;
    }

    if sheet_width > limits.max_sheet_dimension || sheet_height > limits.max_sheet_dimension {
        report.findings.push(Finding::error(format!(
            "лист {sheet_width}×{sheet_height} больше предела {0}×{0}: на слабых GPU он не откроется",
            limits.max_sheet_dimension
        )));
    }

    let texture_bytes = u64::from(sheet_width) * u64::from(sheet_height) * 4;
    if texture_bytes > limits.max_texture_bytes {
        report.findings.push(Finding::error(format!(
            "текстура {} МиБ больше предела {} МиБ",
            texture_bytes / 1024 / 1024,
            limits.max_texture_bytes / 1024 / 1024
        )));
    }

    // Сетка обязана совпадать с файлом: иначе кадры поедут, и заметит это
    // пользователь, а не валидатор.
    let expected_width = u64::from(grid.columns) * u64::from(grid.cell_width);
    let expected_height = u64::from(grid.rows) * u64::from(grid.cell_height);

    if expected_width != u64::from(sheet_width) || expected_height != u64::from(sheet_height) {
        report.findings.push(Finding::error(format!(
            "сетка описывает {expected_width}×{expected_height}, а лист {sheet_width}×{sheet_height}"
        )));
    }
}

fn check_animations(manifest: &Manifest, report: &mut Report, limits: &Limits) {
    let grid = &manifest.grid;

    for (name, animation) in &manifest.animations {
        if animation.frames == 0 {
            report
                .findings
                .push(Finding::error(format!("анимация «{name}» без кадров")));
            continue;
        }

        if animation.frames > limits.max_frames_per_animation {
            report.findings.push(Finding::error(format!(
                "анимация «{name}»: {} кадров при пределе {}",
                animation.frames, limits.max_frames_per_animation
            )));
        }

        if animation.row >= grid.rows {
            report.findings.push(Finding::error(format!(
                "анимация «{name}» ссылается на строку {}, а строк {}",
                animation.row, grid.rows
            )));
        }

        // Поддиапазон обязан помещаться в строку целиком: иначе кадры
        // «перетекают» на следующую строку и показывают чужую позу.
        let last_column = u64::from(animation.start_column) + u64::from(animation.frames);
        if last_column > u64::from(grid.columns) {
            report.findings.push(Finding::error(format!(
                "анимация «{name}»: кадры {}–{} выходят за {} столбцов",
                animation.start_column,
                last_column.saturating_sub(1),
                grid.columns
            )));
        }

        if animation.frame_duration_ms < limits.min_frame_duration_ms {
            report.findings.push(Finding::error(format!(
                "анимация «{name}»: {} мс на кадр — быстрее предела {} мс",
                animation.frame_duration_ms, limits.min_frame_duration_ms
            )));
        }

        if animation.frame_duration_ms > limits.max_frame_duration_ms {
            report.findings.push(Finding::error(format!(
                "анимация «{name}»: {} мс на кадр — дольше предела {} мс",
                animation.frame_duration_ms, limits.max_frame_duration_ms
            )));
        }
    }

    if !manifest
        .animations
        .contains_key(&manifest.fallback_animation)
    {
        report.findings.push(Finding::error(format!(
            "fallbackAnimation «{}» не описана: подменять отсутствующие состояния нечем",
            manifest.fallback_animation
        )));
    }
}

fn check_coverage(manifest: &Manifest, limits: &Limits, report: &mut Report) {
    let grid = &manifest.grid;
    let cells = u64::from(grid.columns) * u64::from(grid.rows);
    if cells == 0 {
        return;
    }

    let declared = manifest.declared_frames();
    let coverage = declared as f64 / cells as f64;

    if coverage < limits.min_declared_coverage {
        report.findings.push(Finding::error(format!(
            "объявлено {declared} кадров из {cells} ячеек — {:.0}% при минимуме {:.0}%: \
             остальное занимает память впустую",
            coverage * 100.0,
            limits.min_declared_coverage * 100.0
        )));
    }
}

fn check_states(manifest: &Manifest, report: &mut Report) {
    let missing: Vec<&str> = REQUIRED_STATES
        .iter()
        .filter(|state| !manifest.animations.contains_key(**state))
        .copied()
        .collect();

    if !missing.is_empty() {
        // Предупреждение, а не ошибка: §FR-8 разрешает подмену через
        // fallbackAnimation. Но питомец, у которого половина состояний
        // выглядит одинаково, — это не то, что автор хотел сделать.
        report.findings.push(Finding::warning(format!(
            "нет анимаций для состояний: {}. Будет показана fallbackAnimation",
            missing.join(", ")
        )));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::petpack::manifest::Manifest;

    fn manifest_json(animations: &str, columns: u32, rows: u32) -> String {
        format!(
            r#"{{
                "schemaVersion": 1,
                "renderer": "sprite-sheet",
                "id": "org.example.pet",
                "name": "Example",
                "version": "1.0.0",
                "sheet": "sheet.png",
                "grid": {{ "columns": {columns}, "rows": {rows}, "cellWidth": 100, "cellHeight": 100 }},
                "animations": {{ {animations} }},
                "fallbackAnimation": "idle",
                "locales": ["en"]
            }}"#
        )
    }

    fn all_states(columns: u32) -> String {
        REQUIRED_STATES
            .iter()
            .enumerate()
            .map(|(index, state)| {
                format!(
                    r#""{state}": {{ "row": {index}, "frames": {columns}, "frameDurationMs": 200 }}"#
                )
            })
            .collect::<Vec<_>>()
            .join(", ")
    }

    fn parse(json: &str) -> Manifest {
        Manifest::parse(json.as_bytes()).expect("манифест разбирается")
    }

    #[test]
    fn accepts_a_well_formed_pack() {
        let json = manifest_json(&all_states(4), 4, 8);
        let report = validate(&parse(&json), 400, 800, &Limits::default());
        assert!(
            report.is_acceptable(),
            "ожидалась приёмка: {:?}",
            report.findings
        );
        assert_eq!(report.warnings().count(), 0);
    }

    #[test]
    fn rejects_future_schema_and_renderer() {
        let json = manifest_json(&all_states(4), 4, 8)
            .replace(r#""schemaVersion": 1"#, r#""schemaVersion": 99"#)
            .replace(r#""renderer": "sprite-sheet""#, r#""renderer": "rive""#);

        let report = validate(&parse(&json), 400, 800, &Limits::default());
        assert!(!report.is_acceptable());
        // Оба отказа внятные, а не один невнятный сбой.
        assert_eq!(report.errors().count(), 2);
    }

    #[test]
    fn rejects_grid_mismatching_the_sheet() {
        let json = manifest_json(&all_states(4), 4, 8);
        // Сетка описывает 400×800, а файл другой.
        let report = validate(&parse(&json), 400, 640, &Limits::default());
        assert!(!report.is_acceptable());
    }

    #[test]
    fn rejects_frames_running_past_the_row() {
        let json = manifest_json(
            r#""idle": { "row": 0, "startColumn": 6, "frames": 4, "frameDurationMs": 200 }"#,
            8,
            2,
        );
        let report = validate(&parse(&json), 800, 200, &Limits::default());
        assert!(
            !report.is_acceptable(),
            "кадры 6–9 не помещаются в 8 столбцов"
        );
    }

    #[test]
    fn rejects_row_outside_the_grid() {
        let json = manifest_json(
            r#""idle": { "row": 99, "frames": 4, "frameDurationMs": 200 }"#,
            4,
            2,
        );
        let report = validate(&parse(&json), 400, 200, &Limits::default());
        assert!(!report.is_acceptable());
    }

    #[test]
    fn rejects_mostly_empty_sheet() {
        // Огромная сетка ради двух кадров: память занята, толку нет.
        let json = manifest_json(
            r#""idle": { "row": 0, "frames": 2, "frameDurationMs": 200 }"#,
            20,
            20,
        );
        let report = validate(&parse(&json), 2000, 2000, &Limits::default());
        assert!(!report.is_acceptable());
        assert!(report.errors().any(|f| f.message.contains("впустую")));
    }

    #[test]
    fn rejects_texture_over_the_memory_limit() {
        let json = manifest_json(&all_states(64), 64, 64);
        // 6400×6400 RGBA — это 156 МиБ, вдесятеро выше предела.
        let report = validate(&parse(&json), 6400, 6400, &Limits::default());
        assert!(!report.is_acceptable());
        assert!(report.errors().any(|f| f.message.contains("текстура")));
    }

    #[test]
    fn rejects_missing_fallback_animation() {
        // Единственная анимация — happy, а fallbackAnimation указывает на idle,
        // которой в пакете нет: подменять отсутствующие состояния будет нечем.
        let json = manifest_json(
            r#""happy": { "row": 0, "frames": 4, "frameDurationMs": 200 }"#,
            4,
            2,
        );

        let report = validate(&parse(&json), 400, 200, &Limits::default());
        assert!(!report.is_acceptable(), "fallbackAnimation не описана");
    }

    #[test]
    fn warns_about_missing_states_but_accepts() {
        let json = manifest_json(
            r#""idle": { "row": 0, "frames": 4, "frameDurationMs": 200 },
               "happy": { "row": 1, "frames": 4, "frameDurationMs": 200 }"#,
            4,
            2,
        );
        let report = validate(&parse(&json), 400, 200, &Limits::default());
        assert!(report.is_acceptable(), "пакет годен, хоть и беден");
        assert_eq!(report.warnings().count(), 1);
    }

    #[test]
    fn rejects_absurd_frame_durations() {
        for duration in [0, 1, 999_999] {
            let json = manifest_json(
                &format!(r#""idle": {{ "row": 0, "frames": 4, "frameDurationMs": {duration} }}"#),
                4,
                2,
            );
            let report = validate(&parse(&json), 400, 200, &Limits::default());
            assert!(!report.is_acceptable(), "{duration} мс на кадр не годится");
        }
    }

    #[test]
    fn reports_every_problem_at_once() {
        // §US-07 требует понятного списка ошибок: чинить пакет по одной
        // проблеме за раз — плохой опыт.
        let json = manifest_json(
            r#""idle": { "row": 99, "frames": 0, "frameDurationMs": 0 }"#,
            4,
            2,
        )
        .replace(r#""renderer": "sprite-sheet""#, r#""renderer": "lottie""#);

        let report = validate(&parse(&json), 400, 200, &Limits::default());
        assert!(report.errors().count() >= 3, "{:?}", report.findings);
    }
}
