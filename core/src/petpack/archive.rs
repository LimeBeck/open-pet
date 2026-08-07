//! Безопасная распаковка Pet Pack (§FR-8).
//!
//! Здесь разбирается архив, который пользователь взял неизвестно где.
//! Предполагать о нём нельзя ничего: ни что имена файлов безобидны,
//! ни что распакованный размер соответствует сжатому, ни что внутри
//! только то, что заявлено.
//!
//! Правило, из которого всё следует: **сначала весь архив проверяется,
//! и только потом хоть что-то попадает на диск.** Иначе отказ на середине
//! оставит пользователю половину чужого пакета в файловой системе.

use super::validate::{Finding, Severity};

use std::collections::BTreeSet;
use std::io::{Cursor, Read};
use std::path::{Component, Path, PathBuf};

/// Пределы распаковки. Отдельно от [`super::validate::Limits`]: те про
/// содержание пакета, эти — про его безопасность.
#[derive(Debug, Clone, Copy)]
pub struct ArchiveLimits {
    /// Сколько файлов может быть в архиве.
    pub max_entries: usize,
    /// Предел распакованного размера одного файла.
    pub max_entry_bytes: u64,
    /// Предел суммарного распакованного размера.
    pub max_total_bytes: u64,
    /// Во сколько раз распакованное может превышать сжатое.
    ///
    /// Главная защита от zip-бомбы: 42-килобайтный архив, разворачивающийся
    /// в терабайты, отличается от честного пакета именно этим отношением.
    pub max_compression_ratio: u64,
}

impl Default for ArchiveLimits {
    fn default() -> Self {
        Self {
            max_entries: 64,
            // Лист — самый крупный файл пакета; предел согласован
            // с пределом текстуры из validate::Limits.
            max_entry_bytes: 24 * 1024 * 1024,
            max_total_bytes: 32 * 1024 * 1024,
            max_compression_ratio: 200,
        }
    }
}

/// Что разрешено лежать в пакете.
///
/// Список закрытый: неизвестный файл отвергает пакет целиком, а не
/// игнорируется. Молчаливое игнорирование означало бы, что через архив
/// можно принести что угодно — оно просто не будет использовано, но
/// окажется на диске пользователя.
fn is_allowed_entry(path: &Path) -> bool {
    let name = path.to_string_lossy();

    if name == "manifest.json" || name == "preview.png" {
        return true;
    }

    // Лист: единственный PNG в корне, имя задаётся манифестом.
    if path.parent() == Some(Path::new("")) && path.extension().is_some_and(|e| e == "png") {
        return true;
    }

    // Локализованные реплики.
    if path.starts_with("phrases") && path.extension().is_some_and(|e| e == "json") {
        return true;
    }

    false
}

/// Проверяет имя записи архива.
///
/// Отвергается всё, что может увести запись за пределы каталога назначения:
/// абсолютные пути, переходы вверх, корневые префиксы. Обратный слэш
/// проверяется отдельно — на Unix он допустимый символ имени, и архив,
/// собранный под Windows, может принести `..\\..\\что-нибудь`.
fn safe_relative_path(raw: &str) -> Result<PathBuf, String> {
    if raw.is_empty() {
        return Err("пустое имя файла".to_string());
    }

    if raw.contains('\\') {
        return Err(format!("имя «{raw}» содержит обратный слэш"));
    }

    if raw.contains('\0') {
        return Err("имя содержит нулевой байт".to_string());
    }

    let path = Path::new(raw);
    let mut safe = PathBuf::new();

    for component in path.components() {
        match component {
            Component::Normal(part) => safe.push(part),
            Component::CurDir => {}
            Component::ParentDir => {
                return Err(format!("имя «{raw}» выходит за пределы пакета"));
            }
            Component::RootDir | Component::Prefix(_) => {
                return Err(format!("имя «{raw}» абсолютное"));
            }
        }
    }

    if safe.as_os_str().is_empty() {
        return Err(format!("имя «{raw}» ни на что не указывает"));
    }

    Ok(safe)
}

/// Проверенное содержимое архива, готовое к записи на диск.
#[derive(Debug, Default)]
pub struct ExtractedPack {
    pub files: Vec<(PathBuf, Vec<u8>)>,
}

impl ExtractedPack {
    pub fn find(&self, name: &str) -> Option<&[u8]> {
        self.files
            .iter()
            .find(|(path, _)| path == Path::new(name))
            .map(|(_, bytes)| bytes.as_slice())
    }
}

/// Распаковывает архив в память, проверяя каждое ограничение по пути.
///
/// В память, а не на диск, намеренно: пакет целиком меньше предела
/// `max_total_bytes`, а решение «годится или нет» принимается до того,
/// как хоть один байт окажется в файловой системе пользователя.
pub fn extract(bytes: &[u8], limits: &ArchiveLimits) -> Result<ExtractedPack, Vec<Finding>> {
    let mut problems = Vec::new();

    let reader = Cursor::new(bytes);
    let mut archive = match zip::ZipArchive::new(reader) {
        Ok(archive) => archive,
        Err(error) => {
            // Сообщение библиотеки не цитируется: оно может содержать
            // имена файлов из чужого архива.
            let _ = error;
            return Err(vec![Finding {
                severity: Severity::Error,
                message: "архив не читается".to_string(),
            }]);
        }
    };

    if archive.len() > limits.max_entries {
        return Err(vec![Finding {
            severity: Severity::Error,
            message: format!(
                "в архиве {} файлов при пределе {}",
                archive.len(),
                limits.max_entries
            ),
        }]);
    }

    let mut pack = ExtractedPack::default();
    let mut total_uncompressed: u64 = 0;
    let mut seen: BTreeSet<PathBuf> = BTreeSet::new();

    for index in 0..archive.len() {
        let mut entry = match archive.by_index(index) {
            Ok(entry) => entry,
            Err(_) => {
                problems.push(Finding {
                    severity: Severity::Error,
                    message: format!("запись {index} не читается"),
                });
                continue;
            }
        };

        if entry.is_dir() {
            continue;
        }

        // Имя берётся сырым, а не через enclosed_name(): проверять надо
        // самим, чтобы отказ был объяснимым, а правило — видимым в коде.
        let raw_name = entry.name().to_string();
        let path = match safe_relative_path(&raw_name) {
            Ok(path) => path,
            Err(reason) => {
                problems.push(Finding {
                    severity: Severity::Error,
                    message: reason,
                });
                continue;
            }
        };

        if !is_allowed_entry(&path) {
            problems.push(Finding {
                severity: Severity::Error,
                message: format!("файл «{}» не входит в состав Pet Pack", path.display()),
            });
            continue;
        }

        if !seen.insert(path.clone()) {
            // Две записи с одним именем: какая окажется на диске, зависит
            // от порядка распаковки. Это способ показать валидатору одно,
            // а приложению другое.
            //
            // Тестом не покрыто: библиотека zip не даёт собрать такой архив
            // своим же писателем. Проверка остаётся, потому что архив можно
            // собрать и не ею.
            problems.push(Finding {
                severity: Severity::Error,
                message: format!("файл «{}» встречается дважды", path.display()),
            });
            continue;
        }

        let declared = entry.size();
        let compressed = entry.compressed_size().max(1);

        if declared > limits.max_entry_bytes {
            problems.push(Finding {
                severity: Severity::Error,
                message: format!(
                    "файл «{}» распаковывается в {} МиБ при пределе {} МиБ",
                    path.display(),
                    declared / 1024 / 1024,
                    limits.max_entry_bytes / 1024 / 1024
                ),
            });
            continue;
        }

        if declared / compressed > limits.max_compression_ratio {
            problems.push(Finding {
                severity: Severity::Error,
                message: format!(
                    "файл «{}» сжат в {} раз — похоже на архивную бомбу",
                    path.display(),
                    declared / compressed
                ),
            });
            continue;
        }

        // Читаем с ограничителем, а не доверяем заявленному размеру:
        // заголовок архива пишет тот же, кто собрал бомбу.
        let remaining = limits.max_total_bytes.saturating_sub(total_uncompressed);
        let mut content = Vec::new();
        let mut limited = entry.by_ref().take(remaining + 1);

        if limited.read_to_end(&mut content).is_err() {
            problems.push(Finding {
                severity: Severity::Error,
                message: format!("файл «{}» не распаковывается", path.display()),
            });
            continue;
        }

        if content.len() as u64 > remaining {
            problems.push(Finding {
                severity: Severity::Error,
                message: format!(
                    "суммарный размер пакета превысил предел {} МиБ",
                    limits.max_total_bytes / 1024 / 1024
                ),
            });
            return Err(problems);
        }

        total_uncompressed += content.len() as u64;
        pack.files.push((path, content));
    }

    if pack.find("manifest.json").is_none() {
        problems.push(Finding {
            severity: Severity::Error,
            message: "в архиве нет manifest.json".to_string(),
        });
    }

    if problems.is_empty() {
        Ok(pack)
    } else {
        Err(problems)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::io::Write;
    use zip::write::SimpleFileOptions;

    fn build_archive(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let mut buffer = Cursor::new(Vec::new());
        {
            let mut writer = zip::ZipWriter::new(&mut buffer);
            let options =
                SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
            for (name, content) in entries {
                writer.start_file(*name, options).unwrap();
                writer.write_all(content).unwrap();
            }
            writer.finish().unwrap();
        }
        buffer.into_inner()
    }

    fn minimal_manifest() -> &'static [u8] {
        br#"{"schemaVersion":1,"renderer":"sprite-sheet"}"#
    }

    #[test]
    fn accepts_a_plain_pack() {
        let archive = build_archive(&[
            ("manifest.json", minimal_manifest()),
            ("sheet.png", b"not really a png, but allowed by name"),
            ("phrases/ru.json", b"{}"),
        ]);

        let pack = extract(&archive, &ArchiveLimits::default()).expect("обычный пакет");
        assert_eq!(pack.files.len(), 3);
        assert!(pack.find("manifest.json").is_some());
    }

    #[test]
    fn rejects_path_traversal() {
        for name in ["../evil.json", "../../etc/passwd", "phrases/../../x.json"] {
            let archive = build_archive(&[("manifest.json", minimal_manifest()), (name, b"x")]);
            let problems = extract(&archive, &ArchiveLimits::default())
                .expect_err(&format!("«{name}» должно отвергаться"));
            assert!(problems
                .iter()
                .any(|f| f.message.contains("выходит за пределы")));
        }
    }

    #[test]
    fn rejects_absolute_paths() {
        let archive =
            build_archive(&[("manifest.json", minimal_manifest()), ("/etc/passwd", b"x")]);
        let problems = extract(&archive, &ArchiveLimits::default()).expect_err("абсолютный путь");
        assert!(problems.iter().any(|f| f.message.contains("абсолютн")));
    }

    #[test]
    fn rejects_windows_style_traversal() {
        // На Unix обратный слэш — обычный символ имени, поэтому проверяется
        // отдельно: архив мог быть собран под Windows.
        let archive = build_archive(&[
            ("manifest.json", minimal_manifest()),
            ("..\\..\\evil.json", b"x"),
        ]);
        let problems = extract(&archive, &ArchiveLimits::default()).expect_err("обратный слэш");
        assert!(problems.iter().any(|f| f.message.contains("обратный слэш")));
    }

    #[test]
    fn rejects_unexpected_files() {
        let archive = build_archive(&[
            ("manifest.json", minimal_manifest()),
            ("install.sh", b"#!/bin/sh\nrm -rf /"),
        ]);
        let problems = extract(&archive, &ArchiveLimits::default()).expect_err("посторонний файл");
        assert!(problems
            .iter()
            .any(|f| f.message.contains("не входит в состав")));
    }

    #[test]
    fn rejects_a_decompression_bomb() {
        // Мегабайт нулей сжимается в тысячи раз — ровно так выглядит бомба.
        let archive = build_archive(&[
            ("manifest.json", minimal_manifest()),
            ("sheet.png", &vec![0u8; 4 * 1024 * 1024]),
        ]);

        let limits = ArchiveLimits {
            max_compression_ratio: 50,
            ..ArchiveLimits::default()
        };

        let problems = extract(&archive, &limits).expect_err("бомба должна отвергаться");
        assert!(problems.iter().any(|f| f.message.contains("бомбу")));
    }

    #[test]
    fn rejects_too_many_entries() {
        let names: Vec<String> = (0..70).map(|i| format!("phrases/{i}.json")).collect();
        let entries: Vec<(&str, &[u8])> = names
            .iter()
            .map(|n| (n.as_str(), b"{}".as_slice()))
            .collect();

        let archive = build_archive(&entries);
        let problems = extract(&archive, &ArchiveLimits::default()).expect_err("слишком много");
        assert!(problems
            .iter()
            .any(|f| f.message.contains("файлов при пределе")));
    }

    #[test]
    fn rejects_pack_without_manifest() {
        let archive = build_archive(&[("sheet.png", b"x")]);
        let problems = extract(&archive, &ArchiveLimits::default()).expect_err("без манифеста");
        assert!(problems.iter().any(|f| f.message.contains("manifest.json")));
    }

    #[test]
    fn rejects_garbage_instead_of_archive() {
        let problems =
            extract(b"this is not a zip file at all", &ArchiveLimits::default()).unwrap_err();
        assert_eq!(problems.len(), 1);
        assert!(problems[0].message.contains("не читается"));
    }

    #[test]
    fn error_messages_do_not_quote_archive_internals() {
        // Сообщение об ошибке уходит в UI и в журнал. Имя файла из чужого
        // архива — уже недоверенные данные; библиотечное сообщение может
        // содержать и больше.
        let problems = extract(b"PK\x03\x04 broken", &ArchiveLimits::default()).unwrap_err();
        assert!(!problems[0].message.contains("PK"));
    }
}
