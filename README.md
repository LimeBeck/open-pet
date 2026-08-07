# open-pet

Desktop AI Pet — постоянно доступный анимированный питомец поверх рабочего стола.
Эталонная среда: **KDE Plasma 6.2+ / Wayland**.

Питомец реагирует на обезличенные признаки активности и системные события, меняет
эмоцию и анимацию, показывает короткие реплики. Работает полностью локально;
LLM — опциональное дополнение, включаемое явно.

> **Статус:** M3. Питомец реагирует на настоящие события системы —
> простой, питание, сон и блокировку. Активное приложение, уведомления
> и медиа появятся в M4.

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
| [`docs/reference-system.md`](docs/reference-system.md) | Эталонная система, методика замеров и отклонения от §7 |
| [`docs/adr/`](docs/adr/README.md) | Принятые архитектурные решения и их процесс |
| [`docs/backlog.md`](docs/backlog.md) | Замеченное, но не запланированное |
| [`spikes/`](spikes/README.md) | Пробы этапа M0 и их результаты |
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
QML UI (ui/qml)
  ↓ PetViewModel
Qt/KDE Desktop Host (apps/desktop-kde)
  ├─ LayerShellQt, tray, input region  (platform/kde-wayland)
  ├─ KIdleTime, UPower, login1         (platform/kde-wayland)
  └─ CoreBridge — узкий C ABI          (platform/contracts)
          ↓ нормализованные DTO
Rust Core (core)
  ├─ state machine, приоритеты, cooldown
  ├─ каталог реплик, история, локали ru/en
  ├─ LLM gateway              (M6)
  └─ Pet Pack validation      (M5)
```

Платформенные детали живут только в адаптерах; через границу host↔core ходят
стабильные POD-структуры, не Qt-типы. Правила границы — [ADR-001](docs/adr/0001-rust-qt-bridge.md).

## Структура репозитория

```text
apps/desktop-kde/          # entry point, tray, packaging
ui/qml/                    # общий QML UI
core/                      # Rust domain core
platform/contracts/        # C ABI между хостом и ядром
platform/kde-wayland/      # LayerShellQt, input region
assets/builtin-pet/        # встроенный питомец Лайм
spikes/                    # пробы M0
docs/                      # спецификация, privacy model, ADR
```

Каталоги `pet-pack/` и `tests/fixtures/` появятся в M5.

## Встроенный питомец

Лайм — пиксельный зелёный лис в тёмном худи. Спрайтовый лист 8×11, ячейка
192×208, все восемь состояний §4.1 отрисовываются из одной текстуры.
Раскладка строк и её слабые места — в
[`assets/builtin-pet/README.md`](assets/builtin-pet/README.md).

Формат встроенного питомца расходится с Pet Pack v1 из §FR-8: там отдельные
WebP на состояние, здесь один лист. Что из этого станет контрактом v1 —
предмет [ADR-005](docs/adr/0005-pet-pack-sprite-sheet.md), решать до M5.

## Сборка

Нужны Qt 6.5+, LayerShellQt, Rust и CMake. Точные версии эталонной системы —
в [`docs/reference-system.md`](docs/reference-system.md).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build
```

Запуск:

```bash
./build/apps/desktop-kde/open-pet
```

Тесты домена не требуют ни Qt, ни Wayland, ни дисплея:

```bash
cargo test --manifest-path core/Cargo.toml
```

### Диагностика

Логи Qt на Plasma по умолчанию уходят в journald. Чтобы увидеть их в терминале:

```bash
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES='openpet.*=true' ./build/apps/desktop-kde/open-pet
```

| Переменная | Действие |
|---|---|
| `OPENPET_NO_REGION` | не задавать input region: окно ловит ввод целиком |
| `OPENPET_NO_TRAY` | не показывать значок в трее |
| `OPENPET_IDLE_SECONDS` | порог простоя в секундах: ждать пять минут ради одного события неразумно |
| `OPENPET_MOCK_EVENTS` | гонять сценарий заглушки — состояния, которых пока не даёт ни один источник |

Питомца нельзя закрыть кликом — у окна нет ни рамки, ни клавиатуры. Выход через
трей или `pkill -x open-pet`.

## Источники событий

| Источник | Механизм | Что даёт | Этап |
|---|---|---|---|
| Простой и возвращение | KIdleTime | время бездействия, переход idle ↔ active | M3 |
| Питание | UPower по системной шине | флаг сети, процент, состояние заряда | M3 |
| Сон и пробуждение | `org.freedesktop.login1` | `PrepareForSleep` | M3 |
| Блокировка экрана | `org.freedesktop.ScreenSaver` | `ActiveChanged` | M3 |
| Активное приложение | KWin module | нормализованный app id | M4, [ADR-003](docs/adr/0003-kwin-integration.md) |
| Уведомления | под вопросом | факт события | M4, [ADR-004](docs/adr/0004-notification-observation.md) |
| Медиа | MPRIS | `playing`/`paused`/`stopped` | M4 |

Каждый источник публикует состояние здоровья (§FR-4). Недоступность —
штатная ситуация: приложение работает без этой capability, а не падает.

## Этапы

| Этап | Содержание | Статус |
|---|---|---|
| M0 | Technical spikes: LayerShellQt, input region, Rust↔C++ bridge | закрыт |
| M1 | Walking skeleton: репозиторий, CI, host, tray, встроенный питомец | закрыт |
| M2 | Behavior core: шаблонные реплики, история, локализация | закрыт |
| M3 | KDE base integration: idle, UPower, sleep/resume, session | **текущий** |
| M4 | Context integrations: KWin active-app, MPRIS, уведомления | — |
| M5 | Pet Pack v1: schema, validator, импорт, локализации | — |
| M6 | LLM gateway: Ollama, OpenAI-compatible, Vertex AI, secrets | — |
| M7 | Hardening и alpha: настройки, diagnostics, perf, packaging | — |

[ADR-003](docs/adr/0003-kwin-integration.md) и [ADR-004](docs/adr/0004-notification-observation.md)
перенесены в M4: они описывают источники событий, а не риски M0.

## Лицензия

[MIT](LICENSE) — и код, и графика встроенного питомца.

Зависимости от Qt 6, KDE Frameworks и LayerShellQt распространяются под LGPL —
при динамической компоновке это совместимо с MIT для нашего кода, но накладывает
обязательства на способ сборки дистрибутивов. Условия фиксируются при packaging (M7).
