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

    /// Пределы процедурного движения (ADR-009). Недоверенный манифест
    /// не должен создавать тысячи точек интерполяции или требовать резерв
    /// размером с экран.
    pub max_keyframes: usize,
    pub min_motion_duration_ms: u32,
    pub max_motion_duration_ms: u32,
    /// Предел смещения в логических пикселях. Ограничивает и резерв
    /// поверхности: на него закладывается место вокруг питомца.
    pub max_motion_offset: f64,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            max_keyframes: 32,
            min_motion_duration_ms: 100,
            max_motion_duration_ms: 10_000,
            // 256 логических пикселей: этого хватает на прыжок вдвое выше
            // питомца и заведомо меньше любого разумного экрана.
            max_motion_offset: 256.0,

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
    fn error(&mut self, message: String) {
        self.findings.push(Finding {
            severity: Severity::Error,
            message,
        });
    }

    fn warning(&mut self, message: String) {
        self.findings.push(Finding {
            severity: Severity::Warning,
            message,
        });
    }

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
    check_motion(manifest, limits, &mut report);
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

/// Проверка процедурного движения (ADR-009).
///
/// Движение описывается данными, но данные приходят из недоверенного пакета:
/// точек может быть сколько угодно, доли — вне диапазона, порядок — обратный.
/// Всё это ломает интерполяцию в хосте, поэтому отсекается здесь.
fn check_motion(manifest: &Manifest, limits: &Limits, report: &mut Report) {
    for (state, animation) in &manifest.animations {
        let Some(motion) = &animation.motion else {
            continue;
        };

        if motion.keyframes.len() < 2 {
            report.error(format!(
                "«{state}»: движению нужны хотя бы две ключевые точки, а их {}",
                motion.keyframes.len()
            ));
            continue;
        }

        if motion.keyframes.len() > limits.max_keyframes {
            report.error(format!(
                "«{state}»: {} ключевых точек при пределе {}",
                motion.keyframes.len(),
                limits.max_keyframes
            ));
            continue;
        }

        if motion.duration_ms < limits.min_motion_duration_ms
            || motion.duration_ms > limits.max_motion_duration_ms
        {
            report.error(format!(
                "«{state}»: длительность движения {} мс вне диапазона {}–{}",
                motion.duration_ms, limits.min_motion_duration_ms, limits.max_motion_duration_ms
            ));
        }

        let first = motion.keyframes.first().expect("длина проверена выше");
        let last = motion.keyframes.last().expect("длина проверена выше");

        if first.at != 0.0 || last.at != 1.0 {
            report.error(format!(
                "«{state}»: движение должно начинаться на 0.0 и заканчиваться на 1.0,                  а идёт с {} по {}",
                first.at, last.at
            ));
        }

        // Зацикленное движение в состоянии покоя работает всегда, а не
        // изредка. Замерено: непрерывное движение стоит 6.8% ядра против
        // 0.3% у неподвижного питомца — втрое больше цели §7. Пакет от этого
        // не становится негодным, но пользователь вправе знать, за что
        // платит батареей.
        if motion.loop_ && state == "idle" {
            report.warning(format!(
                "«{state}»: зацикленное движение в покое работает постоянно — \
                 измеренная цена около 6.8% ядра против 0.3% у неподвижного питомца"
            ));
        }

        // Зацикленное движение, у которого конец не совпадает с началом,
        // даёт рывок на каждом повторе.
        if motion.loop_ && (first.x != last.x || first.y != last.y) {
            report.warning(format!(
                "«{state}»: зацикленное движение не возвращается в исходную точку —                  будет рывок на каждом повторе"
            ));
        }

        let mut previous = -1.0_f64;
        for (index, frame) in motion.keyframes.iter().enumerate() {
            if !(0.0..=1.0).contains(&frame.at) || !frame.at.is_finite() {
                report.error(format!(
                    "«{state}»: точка {index} стоит на {}, а должна быть в 0.0–1.0",
                    frame.at
                ));
                continue;
            }

            if frame.at <= previous {
                report.error(format!(
                    "«{state}»: точки должны идти по возрастанию, а {} стоит после {}",
                    frame.at, previous
                ));
            }
            previous = frame.at;

            if !frame.x.is_finite() || !frame.y.is_finite() {
                report.error(format!("«{state}»: точка {index} задана нечислом"));
                continue;
            }

            if frame.x.abs() > limits.max_motion_offset || frame.y.abs() > limits.max_motion_offset
            {
                report.error(format!(
                    "«{state}»: смещение {},{} превышает предел {} px",
                    frame.x, frame.y, limits.max_motion_offset
                ));
            }
        }
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
    // --- процедурное движение (ADR-009) ---

    fn with_motion(motion_json: &str) -> Manifest {
        let raw = format!(
            r#"{{
                "schemaVersion": 1,
                "renderer": "sprite-sheet",
                "id": "org.example.motion",
                "name": "Motion",
                "version": "1.0.0",
                "sheet": "sheet.png",
                "grid": {{ "columns": 2, "rows": 1, "cellWidth": 64, "cellHeight": 64 }},
                "animations": {{
                    "idle": {{ "row": 0, "frames": 2, "frameDurationMs": 200, "motion": {motion_json} }}
                }},
                "fallbackAnimation": "idle",
                "locales": ["ru"]
            }}"#
        );
        Manifest::parse(raw.as_bytes()).expect("манифест разбирается")
    }

    fn motion_errors(motion_json: &str) -> Vec<String> {
        let manifest = with_motion(motion_json);
        let report = validate(&manifest, 128, 64, &Limits::default());
        report.errors().map(|f| f.message.clone()).collect()
    }

    #[test]
    fn motion_is_optional() {
        // Существующие пакеты продолжают работать без изменений — это
        // условие ADR-009, а не удобство.
        let raw = br#"{
            "schemaVersion": 1, "renderer": "sprite-sheet",
            "id": "a", "name": "A", "version": "1.0.0", "sheet": "s.png",
            "grid": { "columns": 2, "rows": 1, "cellWidth": 64, "cellHeight": 64 },
            "animations": { "idle": { "row": 0, "frames": 2, "frameDurationMs": 200 } },
            "fallbackAnimation": "idle", "locales": ["ru"]
        }"#;
        let manifest = Manifest::parse(raw).expect("разбирается без motion");
        assert!(manifest.animations["idle"].motion.is_none());
    }

    #[test]
    fn valid_motion_passes() {
        let errors = motion_errors(
            r#"{ "durationMs": 600, "loop": true, "keyframes": [
                { "at": 0.0, "x": 0, "y": 0 },
                { "at": 0.5, "x": 0, "y": -24, "easing": "in-out-quad" },
                { "at": 1.0, "x": 0, "y": 0 }
            ] }"#,
        );
        assert!(errors.is_empty(), "{errors:?}");
    }

    #[test]
    fn keyframes_must_be_ordered() {
        // Обратный порядок ломает интерполяцию в хосте: он ищет отрезок
        // по возрастанию доли и не найдёт его.
        let errors = motion_errors(
            r#"{ "durationMs": 600, "keyframes": [
                { "at": 0.0 }, { "at": 0.8 }, { "at": 0.3 }, { "at": 1.0 }
            ] }"#,
        );
        assert!(
            errors.iter().any(|e| e.contains("возрастанию")),
            "{errors:?}"
        );
    }

    #[test]
    fn keyframes_must_span_the_whole_duration() {
        let errors = motion_errors(
            r#"{ "durationMs": 600, "keyframes": [
                { "at": 0.2 }, { "at": 0.9 }
            ] }"#,
        );
        assert!(errors.iter().any(|e| e.contains("0.0")), "{errors:?}");
    }

    #[test]
    fn offsets_are_bounded() {
        // Резерв поверхности закладывается по этим числам: без предела
        // недоверенный пакет потребовал бы окно размером с экран.
        let errors = motion_errors(
            r#"{ "durationMs": 600, "keyframes": [
                { "at": 0.0 }, { "at": 1.0, "y": -9000 }
            ] }"#,
        );
        assert!(
            errors.iter().any(|e| e.contains("превышает предел")),
            "{errors:?}"
        );
    }

    #[test]
    fn too_many_keyframes_rejected() {
        let points: Vec<String> = (0..200)
            .map(|i| format!(r#"{{ "at": {} }}"#, i as f64 / 199.0))
            .collect();
        let errors = motion_errors(&format!(
            r#"{{ "durationMs": 600, "keyframes": [{}] }}"#,
            points.join(",")
        ));
        assert!(
            errors.iter().any(|e| e.contains("при пределе")),
            "{errors:?}"
        );
    }

    #[test]
    fn duration_is_bounded() {
        let fast = motion_errors(r#"{ "durationMs": 1, "keyframes": [{"at":0.0},{"at":1.0}] }"#);
        assert!(fast.iter().any(|e| e.contains("вне диапазона")), "{fast:?}");

        let slow =
            motion_errors(r#"{ "durationMs": 999999, "keyframes": [{"at":0.0},{"at":1.0}] }"#);
        assert!(slow.iter().any(|e| e.contains("вне диапазона")), "{slow:?}");
    }

    #[test]
    fn single_keyframe_is_not_motion() {
        let errors = motion_errors(r#"{ "durationMs": 600, "keyframes": [{"at":0.0}] }"#);
        assert!(
            errors.iter().any(|e| e.contains("две ключевые")),
            "{errors:?}"
        );
    }

    #[test]
    fn looping_motion_in_idle_warns_about_the_cost() {
        // Не отказ: автор вправе так сделать. Но 6.8% ядра против 0.3%
        // у неподвижного питомца — это то, о чём пользователь должен узнать
        // до установки, а не по разряженной батарее.
        let manifest = with_motion(
            r#"{ "durationMs": 1200, "loop": true, "keyframes": [
                { "at": 0.0, "y": 0 }, { "at": 1.0, "y": 0 }
            ] }"#,
        );
        let report = validate(&manifest, 128, 64, &Limits::default());
        assert!(report.is_acceptable());
        assert!(report.warnings().any(|w| w.message.contains("постоянно")));
    }

    #[test]
    fn looping_motion_that_does_not_return_warns() {
        // Не ошибка: пакет работоспособен. Но на каждом повторе будет рывок.
        let manifest = with_motion(
            r#"{ "durationMs": 600, "loop": true, "keyframes": [
                { "at": 0.0, "y": 0 }, { "at": 1.0, "y": -20 }
            ] }"#,
        );
        let report = validate(&manifest, 128, 64, &Limits::default());
        assert!(report.is_acceptable(), "это предупреждение, а не отказ");
        assert!(report.warnings().any(|w| w.message.contains("рывок")));
    }

    #[test]
    fn unknown_easing_is_rejected_by_the_parser() {
        // Набор плавностей закрыт схемой: произвольное значение означало бы
        // выражение в пакете, а Pet Pack — данные (§FR-8).
        let raw = br#"{
            "schemaVersion": 1, "renderer": "sprite-sheet",
            "id": "a", "name": "A", "version": "1.0.0", "sheet": "s.png",
            "grid": { "columns": 2, "rows": 1, "cellWidth": 64, "cellHeight": 64 },
            "animations": { "idle": { "row": 0, "frames": 2, "frameDurationMs": 200,
                "motion": { "durationMs": 600, "keyframes": [
                    { "at": 0.0, "easing": "eval(alert(1))" }, { "at": 1.0 }
                ] } } },
            "fallbackAnimation": "idle", "locales": ["ru"]
        }"#;
        assert!(
            Manifest::parse(raw).is_err(),
            "неизвестная плавность не должна разбираться"
        );
    }

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
