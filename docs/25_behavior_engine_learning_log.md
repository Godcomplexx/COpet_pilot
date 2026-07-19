# Behavior Engine v1 — learning log

## Что мы пытались доказать

Нужно было отделить решение «какое поведение сейчас важнее» от отрисовки и
аппаратных драйверов. Движок должен одинаково обрабатывать события касания,
MPU6050, Focus и Wi‑Fi, не использовать блокирующие задержки и не повторять
фоновые активности слишком часто.

## Задействованные интерфейсы

- MPU6050 по I2C — наклон, сильная встряска и падение;
- TTP223 и энкодер по GPIO — пользовательская активность и подтверждение;
- Focus Mode — состояние READY/RUNNING/PAUSED и фаза WORK/BREAK;
- Wi‑Fi service — переходы STARTING/CONNECTING/RETRY_WAIT и результат;
- ST7789 по SPI — процедурные слои лица и неизменяемый HUD.

Распиновка не менялась.

## Минимальная архитектура

`main/core/copet_behavior.c` — чистый C-модуль без ESP-IDF. Он хранит:

- текущий временный behavior и его deadline;
- level-triggered состояния Focus и Wi‑Fi;
- приоритет P0–P3;
- окно второй встряски 5 секунд;
- время последней активности;
- историю четырёх P3 и cooldown 10 минут;
- детерминированное состояние PRNG.

`app_main.c` только переводит аппаратные enum в события движка. Renderer в
`pip_face_port.c` применяет слои по порядку:

```text
base eyes → mood → gesture → reaction transform → overlay → screen HUD
```

Первая процедурная группа: `alert`, `attentive`, `focused`, `angry`,
`disoriented`, `double_blink`, `look_left`, `look_right`, `acknowledge`,
`connecting`, `zen`, `dice_roll`.

## Как проверить без платы

```powershell
powershell -File test/host/run_tests.ps1
```

Проверяются приоритеты и прерывания, истечение timers, P0 против P1,
edge-triggered Wi‑Fi, Focus fallback, вторая встряска, P3 cancel, cooldown,
история и переполнение `uint32_t`.

Фактический результат: 36 проверок `copet_behavior` и все существующие host-
наборы завершились без ошибок.

## Сборка прошивки

Проект собран ESP-IDF 6.0.1 для target `esp32`:

```text
copet_hardware_test.bin: 0x10c600 bytes
free in app partition:   0x6aa00 bytes (28%)
```

Новых bitmap-ресурсов и динамического выделения памяти в behavior engine нет.

## Как проверить на устройстве

1. После boot screen должен пройти `ALERT` со сканирующей линией.
2. Короткое касание должно показать `ATTENTIVE`; после 30 секунд покоя сначала
   должен быть двойной blink.
3. Наклон должен показать `DIZZY`; первая сильная встряска — `SCARED`, вторая
   в течение 5 секунд — `ANGRY`.
4. При подключении Wi‑Fi лицо сканирует не дольше 10 секунд.
5. В рабочем RUNNING Focus маленькие глаза в верхней панели выглядят
   сосредоточенными; start/pause/resume дают короткий кивок.
6. После 90 секунд без ввода запускается `ZEN` или `DICE`; ввод немедленно
   возвращает обычное состояние.
7. В monitor каждая реальная смена печатается один раз:

```text
behavior: OLD -> NEW source=... priority=P...
```

## Возможные ошибки

- постоянные строки `behavior` в monitor означают, что source ошибочно
  публикует level-состояние как новое событие каждый tick;
- P3 сразу после ввода означает, что не обновился `last_activity_ms`;
- `connecting` дольше 10 секунд означает сброс исходного Wi‑Fi deadline;
- дрожание около порога наклона нужно исправлять hysteresis в motion source,
  а не блокирующей задержкой в behavior engine;
- если глаза видны, но HUD пропал, нарушен порядок renderer layers.

## Что изучено

- разница между edge-triggered событием и level-triggered условием;
- приоритетное вытеснение без очереди устаревших реакций;
- wrap-safe таймеры на `uint32_t`;
- детерминированная псевдослучайность для повторяемых тестов;
- разделение чистой state logic, source adapters и procedural renderer.
