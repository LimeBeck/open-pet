//! Сквозная проверка импорта: настоящий архив с настоящим питомцем.
//!
//! Отдельно от `builtin_pack.rs`: тот проверяет манифест как файл, этот —
//! весь путь от байтов архива до вердикта.

use openpet_core::petpack::{extract, validate, ArchiveLimits, Limits, Manifest};

fn pack_bytes() -> Vec<u8> {
    let path = std::env::var("OPENPET_TEST_PACK").unwrap_or_else(|_| String::from("/nonexistent"));
    std::fs::read(path).unwrap_or_default()
}

#[test]
fn real_pack_survives_the_whole_path() {
    let bytes = pack_bytes();
    if bytes.is_empty() {
        // Тест запускается только при заданном OPENPET_TEST_PACK: держать
        // трёхмегабайтный архив в репозитории ради одной проверки не стоит.
        eprintln!("пропущено: OPENPET_TEST_PACK не задан");
        return;
    }

    let pack = extract(&bytes, &ArchiveLimits::default()).expect("архив распаковывается");
    let raw = pack.find("manifest.json").expect("манифест на месте");
    let manifest = Manifest::parse(raw).expect("манифест разбирается");

    // Размеры берутся из самого листа архива, а не вписаны числами:
    // тест не должен разваливаться от того, что питомца перерисовали.
    let sheet = pack.find(&manifest.sheet).expect("лист на месте");
    let (width, height) = openpet_core::petpack::png_dimensions(sheet).expect("лист — PNG");
    let report = validate(&manifest, width, height, &Limits::default());
    assert!(report.is_acceptable(), "{:?}", report.findings);
}
