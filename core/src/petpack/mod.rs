//! Pet Pack v1: модель, проверка и лимиты (§FR-8).
//!
//! Валидатор — граница безопасности, а не проверка опечаток. Пакет приходит
//! оттуда, где пользователь его взял, и предполагать о нём нельзя ничего.
//!
//! Разделение: [`manifest`] отвечает за разбор, [`validate`] — за решение.
//! Проверка формата и проверка содержания дают разные сообщения, потому что
//! пользователю нужно понимать, что чинить.

pub mod active;
pub mod archive;
pub mod manifest;
pub mod validate;

pub use active::{ActivePack, AnimationFrames, PackStore, SheetSource};
pub use archive::{extract, ArchiveLimits, ExtractedPack};
pub use manifest::{Manifest, ManifestError};
pub use validate::{validate, Finding, Limits, Report, Severity};

/// Размеры PNG из заголовка IHDR.
///
/// Полноценный декодер здесь не нужен и был бы лишним риском: ширина
/// и высота лежат в фиксированных байтах 16..24 сразу после сигнатуры
/// и заголовка чанка. Проверяется сигнатура и то, что чанк действительно
/// IHDR — иначе на вход можно было бы подсунуть что угодно с похожим
/// началом.
///
/// Существует потому, что вызывающий не может знать размеры листа до
/// распаковки архива, а распаковка живёт здесь же. Требовать их параметром
/// значило бы верить числу, которое некому проверить.
pub fn png_dimensions(bytes: &[u8]) -> Option<(u32, u32)> {
    const SIGNATURE: &[u8] = &[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];

    if bytes.len() < 24 || !bytes.starts_with(SIGNATURE) {
        return None;
    }
    if &bytes[12..16] != b"IHDR" {
        return None;
    }

    let width = u32::from_be_bytes(bytes[16..20].try_into().ok()?);
    let height = u32::from_be_bytes(bytes[20..24].try_into().ok()?);

    // Нулевая сторона запрещена самим форматом; такой файл битый.
    if width == 0 || height == 0 {
        return None;
    }

    Some((width, height))
}

#[cfg(test)]
mod png_tests {
    use super::*;

    fn png_header(width: u32, height: u32) -> Vec<u8> {
        let mut bytes = vec![0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];
        bytes.extend_from_slice(&13u32.to_be_bytes());
        bytes.extend_from_slice(b"IHDR");
        bytes.extend_from_slice(&width.to_be_bytes());
        bytes.extend_from_slice(&height.to_be_bytes());
        bytes
    }

    #[test]
    fn reads_dimensions_from_header() {
        assert_eq!(png_dimensions(&png_header(1536, 2288)), Some((1536, 2288)));
    }

    #[test]
    fn rejects_everything_that_is_not_a_png() {
        for bad in [
            &b""[..],
            &b"not a png at all, really"[..],
            &[0x89, b'P', b'N', b'G'][..],
        ] {
            assert_eq!(png_dimensions(bad), None);
        }
    }

    #[test]
    fn rejects_png_signature_without_ihdr() {
        // Сигнатура верная, а чанк другой: доверять такому файлу нельзя.
        let mut bytes = png_header(10, 10);
        bytes[12..16].copy_from_slice(b"IDAT");
        assert_eq!(png_dimensions(&bytes), None);
    }

    #[test]
    fn rejects_zero_sized_image() {
        assert_eq!(png_dimensions(&png_header(0, 100)), None);
        assert_eq!(png_dimensions(&png_header(100, 0)), None);
    }

    #[test]
    fn reads_the_real_builtin_sheet() {
        let sheet = include_bytes!("../../../assets/builtin-pet/lime.png");
        assert_eq!(png_dimensions(sheet), Some((1536, 2288)));
    }
}
