//! Активный Pet Pack и откат при повреждении (§10).
//!
//! Ядро — единственное место, знающее, какая анимация соответствует какому
//! состоянию. UI спрашивает у ядра, а не хранит собственную таблицу: иначе
//! второй формат отрисовки ([ADR-005](../../../docs/adr/0005-pet-pack-sprite-sheet.md))
//! придётся вживлять в QML.

use super::archive::{extract, ArchiveLimits};
use super::manifest::{Manifest, Motion};
use super::sha256::sha256_hex;
use super::validate::{validate, Finding, Limits, Severity};

/// Манифест встроенного питомца вшивается в ядро.
///
/// §4.1 требует показывать питомца сразу, без импорта, а §10 — иметь куда
/// откатиться, когда импортированный пакет окажется повреждён. Один и тот же
/// файл решает обе задачи.
const BUILTIN_MANIFEST: &str = include_str!("../../../assets/builtin-pet/manifest.json");

/// Размеры листа встроенного питомца. Ядро не декодирует изображения —
/// это забота хоста, — поэтому для встроенного они known заранее.
const BUILTIN_SHEET: (u32, u32) = (1536, 1456);

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
    /// Байты листа, ждущие, пока хост запишет их на диск.
    ///
    /// Ядро не пишет файлы: у него нет ни пути к каталогу данных, ни права
    /// решать, куда их класть. Байты живут здесь ровно до того, как хост
    /// их заберёт, и сразу освобождаются — иначе лист висел бы в памяти
    /// дважды, в ядре и в текстуре.
    pending_sheet: Option<Vec<u8>>,
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
            pending_sheet: None,
        }
    }

    pub fn id(&self) -> &str {
        &self.manifest.id
    }

    pub fn source(&self) -> &SheetSource {
        &self.source
    }

    /// Отдаёт байты листа хосту и забывает их.
    ///
    /// Повторный вызов вернёт `None`: это не ошибка, а признак того, что
    /// лист уже на диске.
    pub fn pending_sheet_len(&self) -> usize {
        self.pending_sheet.as_ref().map_or(0, Vec::len)
    }

    pub fn take_sheet(&mut self) -> Option<Vec<u8>> {
        self.pending_sheet.take()
    }

    /// Движение состояния, если оно описано.
    ///
    /// Подмена отсутствующего состояния на `fallbackAnimation` работает
    /// так же, как для кадров: питомец не должен замирать из-за того,
    /// что в пакете нет одного состояния.
    pub fn motion(&self, state: &str) -> Option<&Motion> {
        self.manifest
            .animations
            .get(state)
            .or_else(|| {
                self.manifest
                    .animations
                    .get(&self.manifest.fallback_animation)
            })
            .and_then(|animation| animation.motion.as_ref())
    }

    /// Резерв под траекторию: крайние смещения по всем анимациям пакета.
    ///
    /// Считается один раз при загрузке, а не на каждое движение. Поверхность
    /// получает этот запас и дальше не меняет размер — растягивать её
    /// на каждый прыжок дорого, а прыжки случаются часто (ADR-009).
    ///
    /// Возвращает `(влево, вверх, вправо, вниз)` в логических пикселях,
    /// все значения неотрицательны.
    pub fn motion_envelope(&self) -> (u32, u32, u32, u32) {
        let mut left = 0.0_f64;
        let mut up = 0.0_f64;
        let mut right = 0.0_f64;
        let mut down = 0.0_f64;

        for animation in self.manifest.animations.values() {
            let Some(motion) = &animation.motion else {
                continue;
            };
            for frame in &motion.keyframes {
                // Смещение влево — это отрицательный x, и запас нужен слева.
                left = left.max(-frame.x);
                right = right.max(frame.x);
                up = up.max(-frame.y);
                down = down.max(frame.y);
            }
        }

        let round = |v: f64| v.ceil().max(0.0) as u32;
        (round(left), round(up), round(right), round(down))
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

    pub fn active_mut(&mut self) -> &mut ActivePack {
        &mut self.active
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
    /// Устанавливает пакет из архива.
    ///
    /// Размеры листа читаются из самого PNG, а не приходят параметром:
    /// вызывающий не может знать их до распаковки, которая происходит здесь,
    /// а верить числу, которое некому проверить, — способ обойти проверку
    /// размеров сетки.
    pub fn install(
        &mut self,
        archive: &[u8],
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
        let Some(sheet_bytes) = pack.find(&manifest.sheet) else {
            return Err(vec![Finding {
                severity: Severity::Error,
                message: format!("в архиве нет листа «{}»", manifest.sheet),
            }]);
        };

        // Сумма, если объявлена, обязана совпадать. §9 говорит о хранении
        // проверенного hash пакета — значит, где-то его надо проверять,
        // и единственное место, где виден и манифест, и лист, — здесь.
        if let Some(declared) = &manifest.sheet_sha256 {
            let actual = sha256_hex(sheet_bytes);
            if !actual.eq_ignore_ascii_case(declared) {
                return Err(vec![Finding {
                    severity: Severity::Error,
                    message: format!(
                        "сумма листа не совпадает: манифест обещает {}, файл даёт {}",
                        &declared[..declared.len().min(16)],
                        &actual[..16]
                    ),
                }]);
            }
        }

        let Some((sheet_width, sheet_height)) = super::png_dimensions(sheet_bytes) else {
            return Err(vec![Finding {
                severity: Severity::Error,
                message: format!("«{}» не является PNG", manifest.sheet),
            }]);
        };

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
                pending_sheet: Some(sheet_bytes.to_vec()),
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
    #[test]
    fn swapped_sheet_is_rejected() {
        // Смысл суммы в манифесте: поймать подмену листа после сборки пакета.
        // Без проверки поле выглядело гарантией, ничего не гарантируя.
        use std::io::Write;

        let manifest = format!(
            r#"{{
                "schemaVersion": 1, "renderer": "sprite-sheet",
                "id": "org.example.swap", "name": "S", "version": "1.0.0",
                "sheet": "sheet.png",
                "sheetSha256": "{}",
                "grid": {{ "columns": 1, "rows": 1, "cellWidth": 64, "cellHeight": 64 }},
                "animations": {{ "idle": {{ "row": 0, "frames": 1, "frameDurationMs": 200 }} }},
                "fallbackAnimation": "idle", "locales": ["ru"]
            }}"#,
            // Сумма от чего-то другого, не от листа в архиве.
            super::super::sha256::sha256_hex(b"not this sheet")
        );

        // Минимальный настоящий PNG 64x64, чтобы дело дошло до проверки суммы.
        let mut png = vec![0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];
        png.extend_from_slice(&13u32.to_be_bytes());
        png.extend_from_slice(b"IHDR");
        png.extend_from_slice(&64u32.to_be_bytes());
        png.extend_from_slice(&64u32.to_be_bytes());

        let mut buffer = Vec::new();
        {
            let mut zip = zip::ZipWriter::new(std::io::Cursor::new(&mut buffer));
            let options: zip::write::FileOptions<()> = zip::write::FileOptions::default();
            zip.start_file("manifest.json", options).unwrap();
            zip.write_all(manifest.as_bytes()).unwrap();
            zip.start_file("sheet.png", options).unwrap();
            zip.write_all(&png).unwrap();
            zip.finish().unwrap();
        }

        let mut store = PackStore::new();
        let problems = store
            .install(&buffer, &ArchiveLimits::default(), &Limits::default())
            .expect_err("подменённый лист не должен устанавливаться");

        assert!(
            problems
                .iter()
                .any(|f| f.message.contains("сумма листа не совпадает")),
            "{problems:?}"
        );
    }

    #[test]
    fn envelope_covers_the_extreme_offsets() {
        // Резерв считается по всем анимациям пакета сразу: поверхность
        // получает его один раз и не меняет размер на каждое движение.
        let raw = br#"{
            "schemaVersion": 1, "renderer": "sprite-sheet",
            "id": "a", "name": "A", "version": "1.0.0", "sheet": "s.png",
            "grid": { "columns": 2, "rows": 2, "cellWidth": 64, "cellHeight": 64 },
            "animations": {
                "idle": { "row": 0, "frames": 2, "frameDurationMs": 200,
                    "motion": { "durationMs": 500, "keyframes": [
                        { "at": 0.0, "x": 0, "y": 0 },
                        { "at": 1.0, "x": -8.5, "y": -30 }
                    ] } },
                "happy": { "row": 1, "frames": 2, "frameDurationMs": 200,
                    "motion": { "durationMs": 500, "keyframes": [
                        { "at": 0.0, "x": 0, "y": 0 },
                        { "at": 1.0, "x": 12, "y": 4 }
                    ] } }
            },
            "fallbackAnimation": "idle", "locales": ["ru"]
        }"#;
        let manifest = Manifest::parse(raw).expect("разбирается");
        let pack = ActivePack {
            manifest,
            source: SheetSource::Builtin,
            sheet_width: 128,
            sheet_height: 128,
            pending_sheet: None,
        };

        // Округление вверх: 8.5 px влево требуют 9 px запаса, иначе
        // питомец обрежется на крайнем кадре.
        assert_eq!(pack.motion_envelope(), (9, 30, 12, 4));
    }

    #[test]
    fn pack_without_motion_needs_no_reserve() {
        // Пакет собирается здесь, а не берётся встроенный: тест про отсутствие
        // движения не должен зависеть от того, добавили движение питомцу или
        // нет. На встроенном он уже однажды сломался именно так.
        let raw = br#"{
            "schemaVersion": 1, "renderer": "sprite-sheet",
            "id": "a", "name": "A", "version": "1.0.0", "sheet": "s.png",
            "grid": { "columns": 1, "rows": 1, "cellWidth": 64, "cellHeight": 64 },
            "animations": { "idle": { "row": 0, "frames": 1, "frameDurationMs": 200 } },
            "fallbackAnimation": "idle", "locales": ["ru"]
        }"#;
        let pack = ActivePack {
            manifest: Manifest::parse(raw).expect("разбирается"),
            source: SheetSource::Builtin,
            sheet_width: 64,
            sheet_height: 64,
            pending_sheet: None,
        };
        assert_eq!(pack.motion_envelope(), (0, 0, 0, 0));
    }

    #[test]
    fn builtin_pet_reserves_room_for_its_jump() {
        // А это уже про встроенного: прыжок объявлен, значит резерв обязан
        // быть — иначе питомец обрежется на верхней точке.
        let store = PackStore::new();
        let (left, top, right, bottom) = store.active().motion_envelope();
        assert!(top > 0, "прыжку нужен запас сверху");
        assert_eq!(
            (left, right, bottom),
            (0, 0, 0),
            "в стороны он не двигается"
        );
    }

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
