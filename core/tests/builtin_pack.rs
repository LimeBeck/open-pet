//! Встроенный питомец обязан проходить собственный валидатор.
//!
//! Проверка не формальная: именно она поймала, что манифест Лайма был написан
//! до появления схемы v1 и не содержал ни `schemaVersion`, ни `renderer`.
//! Валидатор, который не применяют к своему же ассету, быстро расходится
//! с реальностью.

use openpet_core::petpack::{validate, Limits, Manifest, Severity};

/// Размеры листа встроенного питомца. Декодировать PNG ядру нечем и незачем —
/// размеры приходят снаружи, как и от хоста при настоящем импорте.
/// Размеры читаются из самого листа, а не вписаны числами: иначе любая
/// правка питомца ломала бы тесты, к содержимому листа не относящиеся.
fn sheet_size() -> (u32, u32) {
    let sheet = include_bytes!("../../assets/builtin-pet/lime.png");
    openpet_core::petpack::png_dimensions(sheet).expect("встроенный лист — PNG")
}

fn builtin_manifest() -> Manifest {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../assets/builtin-pet/manifest.json"
    );
    let raw = std::fs::read(path).expect("манифест встроенного питомца читается");
    Manifest::parse(&raw).expect("манифест встроенного питомца разбирается")
}

#[test]
fn builtin_pet_passes_its_own_validator() {
    let manifest = builtin_manifest();
    let report = validate(
        &manifest,
        sheet_size().0,
        sheet_size().1,
        &Limits::default(),
    );

    let problems: Vec<&str> = report.findings.iter().map(|f| f.message.as_str()).collect();
    assert!(
        report.is_acceptable(),
        "встроенный питомец не проходит валидатор: {problems:?}"
    );
    assert!(
        !report
            .findings
            .iter()
            .any(|f| f.severity == Severity::Warning),
        "у встроенного питомца не должно быть даже предупреждений: {problems:?}"
    );
}

#[test]
fn builtin_pet_declares_every_state() {
    let manifest = builtin_manifest();
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
        assert!(
            manifest.animations.contains_key(state),
            "во встроенном питомце нет анимации для состояния {state}"
        );
    }
}

#[test]
fn builtin_pet_keeps_coverage_margin() {
    // Порог покрытия — 40%, у Лайма 49%. Запас невелик, и если кто-то
    // уберёт анимацию, не тронув сетку, пакет перестанет проходить.
    // Тест существует, чтобы это заметил не пользователь.
    let manifest = builtin_manifest();
    let cells = u64::from(manifest.grid.columns) * u64::from(manifest.grid.rows);
    let coverage = manifest.declared_frames() as f64 / cells as f64;
    assert!(
        coverage >= 0.45,
        "покрытие {:.0}% опасно близко к порогу 40%",
        coverage * 100.0
    );
}
