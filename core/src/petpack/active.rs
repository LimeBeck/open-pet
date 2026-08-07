//! Активный Pet Pack и откат при повреждении (§10).
//!
//! Ядро — единственное место, знающее, какая анимация соответствует какому
//! состоянию. UI спрашивает у ядра, а не хранит собственную таблицу: иначе
//! второй формат отрисовки ([ADR-005](../../../docs/adr/0005-pet-pack-sprite-sheet.md))
//! придётся вживлять в QML.

use super::archive::{extract, ArchiveLimits};
use super::manifest::Manifest;
use super::validate::{validate, Finding, Limits, Severity};

/// Манифест встроенного питомца вшивается в ядро.
///
/// §4.1 требует показывать питомца сразу, без импорта, а §10 — иметь куда
/// откатиться, когда импортированный пакет окажется повреждён. Один и тот же
/// файл решает обе задачи.
const BUILTIN_MANIFEST: &str = include_str!("../../../assets/builtin-pet/manifest.json");

/// Размеры листа встроенного питомца. Ядро не декодирует изображения —
/// это забота хоста, — поэтому для встроенного они known заранее.
const BUILTIN_SHEET: (u32, u32) = (1536, 2288);

/// Где лежит лист активного пакета.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SheetSource {
    /// Вшит в приложение.
    Builtin,
    /// Импортирован: имя файла внутри каталога пакета.
    Imported { file: String },
}

/// Кадры одной анимации в координатах листа.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AnimationFrames {
    pub row: u32,
    pub start_column: u32,
    pub frames: u32,
    pub frame_duration_ms: u32,
}

pub struct ActivePack {
    manifest: Manifest,
    source: SheetSource,
    sheet_width: u32,
    sheet_height: u32,
}

impl Default for ActivePack {
    fn default() -> Self {
        Self::builtin()
    }
}

impl ActivePack {
    /// Встроенный питомец. Разбор здесь не может провалиться на рабочей
    /// сборке: манифест лежит рядом с кодом и проверяется тестом
    /// `builtin_pack.rs`. Если он всё же не разберётся, лучше упасть при
    /// запуске, чем показывать пустое окно.
    pub fn builtin() -> Self {
        let manifest = Manifest::parse(BUILTIN_MANIFEST.as_bytes())
            .expect("манифест встроенного питомца обязан разбираться");

        Self {
            manifest,
            source: SheetSource::Builtin,
            sheet_width: BUILTIN_SHEET.0,
            sheet_height: BUILTIN_SHEET.1,
        }
    }

    pub fn id(&self) -> &str {
        &self.manifest.id
    }

    pub fn source(&self) -> &SheetSource {
        &self.source
    }

    pub fn sheet_size(&self) -> (u32, u32) {
        (self.sheet_width, self.sheet_height)
    }

    pub fn grid_cell(&self) -> (u32, u32) {
        (
            self.manifest.grid.cell_width,
            self.manifest.grid.cell_height,
        )
    }

    /// Кадры для состояния. Неизвестное состояние подменяется
    /// `fallbackAnimation` — отсутствие анимации не должно оставлять
    /// пустое окно (§FR-8).
    pub fn animation(&self, state: &str) -> AnimationFrames {
        let animation = self
            .manifest
            .animations
            .get(state)
            .or_else(|| {
                self.manifest
                    .animations
                    .get(&self.manifest.fallback_animation)
            })
            .or_else(|| self.manifest.animations.values().next())
            .expect("валидатор гарантирует хотя бы одну анимацию");

        AnimationFrames {
            row: animation.row,
            start_column: animation.start_column,
            frames: animation.frames,
            frame_duration_ms: animation.frame_duration_ms,
        }
    }
}

/// Хранит активный пакет и умеет откатываться (§10).
pub struct PackStore {
    active: ActivePack,
    /// Последний пакет, который работал. Встроенный годится всегда,
    /// поэтому Option не нужен.
    fallback: ActivePack,
}

impl Default for PackStore {
    fn default() -> Self {
        Self::new()
    }
}

impl PackStore {
    pub fn new() -> Self {
        Self {
            active: ActivePack::builtin(),
            fallback: ActivePack::builtin(),
        }
    }

    pub fn active(&self) -> &ActivePack {
        &self.active
    }

    /// Проверяет и устанавливает пакет из байтов архива.
    ///
    /// Размеры листа приходят снаружи: декодировать PNG ядру нечем.
    /// Хост декодирует заголовок картинки и сообщает размеры сюда.
    ///
    /// При любой ошибке активный пакет **не меняется**: сначала всё
    /// проверяется, и только успешная проверка приводит к подмене.
    pub fn install(
        &mut self,
        archive: &[u8],
        sheet_width: u32,
        sheet_height: u32,
        archive_limits: &ArchiveLimits,
        limits: &Limits,
    ) -> Result<Vec<Finding>, Vec<Finding>> {
        let pack = extract(archive, archive_limits)?;

        let Some(raw) = pack.find("manifest.json") else {
            return Err(vec![Finding {
                severity: Severity::Error,
                message: "в архиве нет manifest.json".to_string(),
            }]);
        };

        let manifest = Manifest::parse(raw).map_err(|error| {
            vec![Finding {
                severity: Severity::Error,
                message: error.to_string(),
            }]
        })?;

        // Лист обязан присутствовать под тем именем, которое назвал манифест:
        // иначе пакет пройдёт проверку и не покажет ничего.
        if pack.find(&manifest.sheet).is_none() {
            return Err(vec![Finding {
                severity: Severity::Error,
                message: format!("в архиве нет листа «{}»", manifest.sheet),
            }]);
        }

        let report = validate(&manifest, sheet_width, sheet_height, limits);
        if !report.is_acceptable() {
            return Err(report.findings);
        }

        let sheet_file = manifest.sheet.clone();

        // Прежний активный становится запасным только если сам был исправен.
        // Встроенный исправен всегда, поэтому откатываться есть куда даже
        // после нескольких неудачных импортов подряд.
        self.fallback = std::mem::replace(
            &mut self.active,
            ActivePack {
                manifest,
                source: SheetSource::Imported { file: sheet_file },
                sheet_width,
                sheet_height,
            },
        );

        Ok(report.findings)
    }

    /// Откат к последнему рабочему пакету (§10: «повреждён — откат
    /// на последний валидный или встроенный»).
    pub fn rollback(&mut self) {
        // take, а не replace: Default для ActivePack и есть встроенный питомец.
        self.active = std::mem::take(&mut self.fallback);
    }

    pub fn reset_to_builtin(&mut self) {
        self.active = ActivePack::builtin();
        self.fallback = ActivePack::builtin();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_serves_every_state() {
        let pack = ActivePack::builtin();
        for state in [
            "idle",
            "happy",
            "curious",
            "sleepy",
            "charging",
            "low_battery",
            "notification",
            "busy",
        ] {
            let frames = pack.animation(state);
            assert!(frames.frames > 0, "у состояния {state} нет кадров");
        }
    }

    #[test]
    fn unknown_state_falls_back_instead_of_failing() {
        let pack = ActivePack::builtin();
        let unknown = pack.animation("несуществующее_состояние");
        let fallback = pack.animation("idle");
        assert_eq!(unknown, fallback, "неизвестное состояние берёт fallback");
    }

    #[test]
    fn failed_install_leaves_the_active_pack_alone() {
        let mut store = PackStore::new();
        let before = store.active().id().to_string();

        let problems = store
            .install(
                "это не архив".as_bytes(),
                100,
                100,
                &ArchiveLimits::default(),
                &Limits::default(),
            )
            .expect_err("мусор не устанавливается");

        assert!(!problems.is_empty());
        assert_eq!(
            store.active().id(),
            before,
            "неудачный импорт не должен подменять активный пакет"
        );
    }

    #[test]
    fn rollback_always_has_somewhere_to_go() {
        let mut store = PackStore::new();
        // Даже без единого успешного импорта откат возможен: встроенный
        // питомец исправен всегда.
        store.rollback();
        assert_eq!(store.active().id(), "dev.limebeck.lime");
        store.rollback();
        assert_eq!(store.active().id(), "dev.limebeck.lime");
    }
}
