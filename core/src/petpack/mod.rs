//! Pet Pack v1: модель, проверка и лимиты (§FR-8).
//!
//! Валидатор — граница безопасности, а не проверка опечаток. Пакет приходит
//! оттуда, где пользователь его взял, и предполагать о нём нельзя ничего.
//!
//! Разделение: [`manifest`] отвечает за разбор, [`validate`] — за решение.
//! Проверка формата и проверка содержания дают разные сообщения, потому что
//! пользователю нужно понимать, что чинить.

pub mod archive;
pub mod manifest;
pub mod validate;

pub use archive::{extract, ArchiveLimits, ExtractedPack};
pub use manifest::{Manifest, ManifestError};
pub use validate::{validate, Finding, Limits, Report, Severity};
