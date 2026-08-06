# open-pet

Desktop AI Pet — постоянно доступный анимированный питомец поверх рабочего стола.
Эталонная среда: **KDE Plasma 6.2+ / Wayland**.

Питомец реагирует на обезличенные признаки активности и системные события, меняет
эмоцию и анимацию, показывает короткие реплики. Работает полностью локально;
LLM — опциональное дополнение, включаемое явно.

> **Статус:** документация. Кода пока нет, текущий этап — **M0. Technical spikes**.

## Источник истины

Разработка ведётся по спецификации и roadmap MVP:

→ [`docs/desktop-ai-pet-mvp-spec-roadmap.md`](docs/desktop-ai-pet-mvp-spec-roadmap.md) (Draft v0.1)

Документ фиксирует продуктовые границы, privacy-модель, архитектуру, функциональные
требования, Definition of Done и этапы M0–M7. Принятие зафиксировано в
[ADR-000](docs/adr/0000-adopt-mvp-roadmap.md). Отклонения от него оформляются
новым ADR — см. [docs/adr/](docs/adr/README.md).

## Карта документации

| Документ | Назначение |
|---|---|
| [`docs/desktop-ai-pet-mvp-spec-roadmap.md`](docs/desktop-ai-pet-mvp-spec-roadmap.md) | Спецификация и roadmap MVP. Источник истины |
| [`docs/privacy-model.md`](docs/privacy-model.md) | Что наблюдается, что нет, что хранится, что уходит в LLM |
| [`docs/reference-system.md`](docs/reference-system.md) | Эталонная система для замеров и E2E. Заполняется по итогам M0 |
| [`docs/adr/`](docs/adr/README.md) | Принятые архитектурные решения и их процесс |
| [`AGENTS.md`](AGENTS.md) | Правила работы в репозитории для людей и агентов |

## Что делает MVP

- Прозрачный overlay без рамки и записи в панели задач, у выбранного края экрана.
- Восемь состояний: `idle`, `happy`, `curious`, `sleepy`, `charging`, `low_battery`,
  `notification`, `busy`.
- Локальный rule engine с приоритетами, cooldown и защитой от спама.
- Шаблонные реплики `ru`/`en`; LLM (Ollama, OpenAI-compatible, Vertex AI) — опционально,
  с обязательным fallback на шаблон.
- Импорт и валидация Pet Pack v1 — данные, без исполняемого кода.

Границы MVP — §4 спецификации. Всё, что не входит, перечислено явно в §4.3.

## Privacy в двух строках

Приложение видит **факт** активности, смены приложения, уведомления и воспроизведения
медиа. Оно не читает клавиши, текст, заголовки окон, содержимое уведомлений, экран,
буфер обмена и аудио. Сеть не используется, пока пользователь явно не включит LLM.
Подробности — [`docs/privacy-model.md`](docs/privacy-model.md).

## Архитектура

```text
QML UI
  ↓ UI intents / ViewModel
Qt/KDE Desktop Host (C++)
  ├─ LayerShellQt, tray, KIdleTime, D-Bus
  ├─ KDE Wayland adapters
  └─ thin Rust bridge
          ↓ normalized DTO
Rust Core
  ├─ behavior/state machine
  ├─ phrase/template engine
  ├─ LLM gateway
  ├─ Pet Pack validation
  └─ config domain model
```

Платформенные детали живут только в адаптерах; через границу host↔core ходят
стабильные DTO, не Qt-типы.

## Целевая структура репозитория

Появляется постепенно, начиная с M1:

```text
apps/desktop-kde/          # entry point, packaging
ui/qml/                    # общий QML UI
core/                      # Rust domain core
platform/contracts/        # capability и DTO-контракты
platform/kde-wayland/      # Qt/KF6/LayerShellQt adapters
pet-pack/schema/           # JSON Schema
pet-pack/validator/        # безопасная проверка пакетов
assets/builtin-pet/        # встроенный эталонный пакет
tests/fixtures/            # события и Pet Pack fixtures
docs/                      # спецификация, privacy model, ADR
```

## Этапы

| Этап | Содержание | Статус |
|---|---|---|
| M0 | Technical spikes: LayerShellQt, QML animation, KIdleTime, Rust↔C++ bridge | **текущий** |
| M1 | Walking skeleton: репозиторий, CI, host, tray, встроенный питомец | — |
| M2 | Behavior core: state machine, приоритеты, cooldown, шаблоны | — |
| M3 | KDE base integration: idle, UPower, sleep/resume, session | — |
| M4 | Context integrations: KWin active-app, MPRIS, уведомления | — |
| M5 | Pet Pack v1: schema, validator, импорт, локализации | — |
| M6 | LLM gateway: Ollama, OpenAI-compatible, Vertex AI, secrets | — |
| M7 | Hardening и alpha: настройки, diagnostics, perf, packaging | — |

Ближайшая работа по §16 спецификации — закрыть spikes и превратить
[ADR-001…004](docs/adr/README.md) из `Proposed` в `Accepted`, затем пересмотреть
оценки M1–M7.

## Сборка

Пока нечего собирать. Требования к окружению фиксируются в
[`docs/reference-system.md`](docs/reference-system.md) по итогам M0.

## Лицензия

[MIT](LICENSE).

Зависимости от Qt 6, KDE Frameworks и LayerShellQt распространяются под LGPL —
при динамической компоновке это совместимо с MIT для нашего кода, но накладывает
обязательства на способ сборки дистрибутивов. Условия фиксируются при packaging (M7).
