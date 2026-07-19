# CoPet Behavior Engine v1 — первая процедурная группа

**Author:** Codex совместно с владельцем CoPet Pilot  
**Date:** 2026-07-19  
**Status:** Approved  
**Reviewers:** владелец CoPet Pilot

## Context

В прошивке уже работают процедурное лицо Desk Mode, реакции MPU6050,
Focus 25/5, Wi‑Fi Station и звуки. Сейчас выбор выражения, жеста и эффекта
распределён между `desk_mode.c`, `app_main.c` и `pip_face_port.c`. Из-за этого
невозможно единообразно решить, что должно прервать текущую анимацию, когда
вернуться к состоянию режима и как не повторять фоновые активности.

Эта итерация вводит чистый C-модуль `copet_behavior`, который получает
события, разрешает приоритеты P0–P3 и выдаёт одно итоговое процедурное
представление для renderer. Модуль не зависит от ESP-IDF, поэтому правила
приоритетов, таймеров и cooldown проверяются host-тестами до прошивки платы.

Первая группа состоит ровно из 12 поведений. Она намеренно использует только
уже доступные источники: boot, touch/encoder, MPU6050, Focus и Wi‑Fi. Тяжёлые
кадровые анимации и SD-карта в эту итерацию не входят.

Термины `MUST`, `MUST NOT`, `SHOULD`, `SHOULD NOT` и `MAY` используются в
смысле RFC 2119.

### Утверждаемая группа из 12 поведений

Координаты ниже заданы в логическом пространстве лица 128×64 и затем
масштабируются существующим renderer под область экрана.

| ID | Слой | Приоритет | Точный внешний вид | Триггер и длительность |
|---|---|---:|---|---|
| `alert` | mood + overlay | P1 | оба глаза 30×44; взгляд по центру; тонкая вертикальная линия проходит слева направо по всей области лица; в конце глаза возвращаются к 36×36 | один раз после завершения boot screen; 1600 мс |
| `attentive` | mood | P1 | глаза 38×42; взгляд по центру; высота плавно пульсирует в пределах 96–104%, без тряски | короткое касание в Desk; 3000 мс или до следующего явного ввода |
| `focused` | mood | P2 | глаза 40×24; верхние веки слегка опущены к внешним краям; взгляд на 3 единицы вниз; амплитуда фонового движения не больше 1 единицы | Focus, рабочая фаза в состоянии RUNNING; пока условие активно |
| `angry` | mood + transform | P1 | глаза 40×28; верхние веки наклонены вниз к центру; первые 350 мс вся группа дрожит по X на ±3 единицы, затем неподвижна | вторая сильная встряска в течение 5 с после первой; 1800 мс |
| `disoriented` | mood | P1 | глаза 32×36; левый и правый глаз смещены по Y на противоположные ±3 единицы; взгляд делает один круг радиусом 6 единиц и возвращается в центр | устойчивый наклон MPU6050; 1800 мс |
| `double_blink` | gesture | P1 | оба глаза синхронно закрываются, открываются и повторяют цикл: 0–80 мс закрытие, 80–160 мс открытие, 240–320 мс закрытие, 320–480 мс открытие | первое касание после неактивности не меньше 30 с; 480 мс, после него продолжается `attentive` |
| `look_left` | gesture | P1 | форма текущих глаз не меняется; взгляд с easing смещается до X=-9 за 150 мс, держится 750 мс и возвращается за 300 мс | один шаг энкодера влево в Desk либо подтверждённый наклон влево; 1200 мс |
| `look_right` | gesture | P1 | форма текущих глаз не меняется; взгляд с easing смещается до X=+9 за 150 мс, держится 750 мс и возвращается за 300 мс | один шаг энкодера вправо в Desk либо подтверждённый наклон вправо; 1200 мс |
| `acknowledge` | reaction transform | P1 | вся группа глаз делает короткий кивок: Y=0→+4→0; в нижней точке высота глаз уменьшается до 75% и восстанавливается | подтверждение пункта меню, переключение Settings или start/pause/resume Focus; 450 мс |
| `connecting` | action + overlay | P1 | глаза 34×34 следуют за вертикальной линией сканирования X=12→116; полный проход 800 мс и повторяется; HUD Wi‑Fi остаётся видимым | переход Wi‑Fi в STARTING, CONNECTING или RETRY_WAIT; до выхода из этих статусов, максимум 10 с |
| `zen` | P3 action | P3 | глаза превращаются в две горизонтальные округлые щели 34×5; группа плавно поднимается и опускается на 2 единицы с периодом 2,4 с; над центром по очереди появляются три точки | выбор P3-планировщика после 90 с бездействия; 12 с |
| `dice_roll` | P3 action | P3 | глаза становятся двумя квадратами 30×30; внутри на каждом кадре 120 мс показываются 1–6 точек; последние 1200 мс значение фиксировано, затем обычные глаза восстанавливаются | выбор P3-планировщика после 90 с бездействия; 8 с |

Существующие `neutral`, `scared`, `bored`, `sleepy`, `shiver`, `overheated`,
обычный `blink` и `smoking` сохраняются через адаптер как legacy-поведения,
но не считаются новыми элементами этой группы. P0-реакция `scared + shiver`
остаётся защитной и участвует в проверке прерываний.

## Functional Requirements

- FR-1: Прошивка MUST содержать независимый от ESP-IDF модуль
  `main/core/copet_behavior.c/.h`, который принимает события, время в
  миллисекундах и контекст режима, а возвращает неизменяемый view для UI.
- FR-2: Движок MUST поддерживать приоритеты P0, P1, P2 и P3. Более высокий
  приоритет MUST немедленно прерывать более низкий; более низкий MUST NOT
  вытеснять активный более высокий.
- FR-3: При одинаковом приоритете новое явное пользовательское или системное
  событие MUST заменить старое временное поведение. Повтор того же
  level-triggered состояния MUST NOT перезапускать его таймер каждый tick.
- FR-4: Движок MUST использовать неблокирующие таймеры на `uint32_t` с
  wrap-safe сравнением и MUST NOT вызывать `vTaskDelay`, `sleep` или ожидание.
- FR-5: Все 12 поведений из утверждаемой таблицы MUST иметь указанные
  триггеры, длительности и визуальные параметры; изменения этих параметров
  после Approved требуют правки этой спецификации.
- FR-6: Renderer MUST быть разделён на последовательные слои: background,
  base eyes/mood, gesture modifiers, reaction transform, action/vibe overlay,
  screen HUD. Слой HUD MUST рисоваться последним и оставаться читаемым.
- FR-7: События MPU6050 MUST различать сильную встряску, падение и направление
  устойчивого наклона. Вторая сильная встряска в окне 5 с MUST запускать
  `angry`; падение MUST запускать legacy P0 `scared + shiver`.
- FR-8: Focus MUST публиковать события при смене `READY/RUNNING/PAUSED` и
  рабочей/перерывной фазы. Рабочий RUNNING MUST поддерживать `focused` P2;
  каждое start/pause/resume MUST сначала показывать `acknowledge` P1, затем
  возвращать актуальное P2-состояние без сброса таймера Focus.
- FR-9: Wi‑Fi MUST публиковать событие только при изменении
  `wifi_service_status_t`. STARTING, CONNECTING и RETRY_WAIT MUST активировать
  `connecting`; CONNECTED, ERROR, OFF и NO_CREDENTIALS MUST завершать его.
- FR-10: P3-планировщик MUST работать только в Desk Mode, начинать выбор после
  90 с бездействия, запускать только `zen` или `dice_roll`, а после завершения
  планировать следующую попытку через псевдослучайные 60–180 с.
- FR-11: P3-планировщик MUST хранить последние четыре завершённых ID, MUST
  запрещать немедленный повтор и MUST соблюдать cooldown одного ID 10 минут.
  Кандидаты из history SHOULD исключаться. Поскольку первый пул содержит
  только два ID, если после cooldown оба находятся в history, MUST выбираться
  тот, который завершался раньше. Если cooldown не прошёл ни у одного,
  планировщик MUST оставить базовое состояние и повторить выбор при следующем
  сроке.
- FR-12: Любой touch, encoder event или переход из Desk MUST немедленно
  завершать P3, сбрасывать отсчёт бездействия и назначать следующий P3 не
  раньше чем через 90 с новой неактивности.
- FR-13: Host suite MUST проверять приоритеты, прерывание, истечение таймеров,
  wrap-around, cooldown, историю повторов, P3 cancel и edge-triggered события
  Focus/Wi‑Fi. Проект MUST собираться ESP-IDF 6.0.1 без новых warnings.
- FR-14: Каждая смена активного behavior ID SHOULD писать одну строку INFO
  `behavior: OLD -> NEW source=... priority=P...`; неизменившийся tick MUST
  NOT создавать повторные строки.

## Non-Functional Requirements

- NFR-1: Движок MUST выполнять `update` за O(N), где N — фиксированное число
  зарегистрированных поведений, без динамического выделения памяти.
- NFR-2: Полное состояние движка, включая историю и timers, SHOULD занимать
  не более 512 байт RAM; framebuffer в этот лимит не входит.
- NFR-3: Процедурный renderer MUST работать без новых bitmap-ресурсов и без
  доступа к SD-карте.
- NFR-4: Desk и анимированная часть Focus SHOULD обновляться с частотой не
  ниже 8 fps, при этом input, Focus timer и Wi‑Fi service MUST оставаться
  неблокирующими.
- NFR-5: Псевдослучайность P3 MUST быть инъецируемой или seedable, чтобы host-
  тесты были полностью повторяемыми.
- NFR-6: Отказ MPU6050 или отключённый Wi‑Fi MUST NOT останавливать базовую
  анимацию, Focus timer или меню.

## Acceptance Criteria

### AC-1: Детерминированный lifecycle поведения (FR-1, FR-3, FR-4, FR-5)

**Given** новый движок с временем 1000 мс и базовым `neutral`  
**When** подаётся событие `BOOT_COMPLETED`, затем время переводится на 2599 и
2600 мс  
**Then** активен `alert` до 2599 мс включительно, а на 2600 мс он завершён и
view возвращён к базовому состоянию без блокирующей задержки.

### AC-2: P0 прерывает любое декоративное поведение (FR-2, FR-7)

**Given** активен `dice_roll` P3 или `focused` P2  
**When** поступает `MOTION_FALLING`  
**Then** в тот же вызов `update` активируется legacy `scared + shiver` P0, а
прерванное временное поведение не возобновляется автоматически.

### AC-3: Правила P1, равного и нижнего приоритета (FR-2, FR-3)

**Given** активен `connecting` P1  
**When** P3 становится due, затем пользователь подтверждает пункт меню  
**Then** P3 не вытесняет P1, а явный `acknowledge` P1 заменяет `connecting` на
450 мс; после него всё ещё активное состояние Wi‑Fi снова разрешается как
`connecting`, но его исходный deadline 10 с не начинается заново.

### AC-4: Процедурные параметры и слои renderer (FR-5, FR-6)

**Given** view каждого из 12 behavior ID в начале, середине и конце его
timeline  
**When** renderer формирует кадр 128×64 и экранный кадр 240×240  
**Then** размеры, offsets, easing phases и overlays соответствуют таблице,
выходы за границы клипуются, а HUD Desk/Focus рисуется поверх лица и читается.

### AC-5: Focus является level-triggered источником (FR-8)

**Given** Focus READY в рабочей фазе  
**When** пользователь запускает Focus, ждёт больше 450 мс, ставит на паузу и
затем возобновляет  
**Then** каждая команда даёт один `acknowledge`, между командами рабочий
RUNNING показывает `focused`, PAUSED не перезапускает `focused`, а оставшееся
время Focus не меняется из-за behavior engine.

### AC-6: Wi‑Fi является edge-triggered источником (FR-9, FR-14)

**Given** статус Wi‑Fi меняется STARTING → CONNECTING и затем 100 раз читается
как CONNECTING  
**When** источник передаёт статусы движку  
**Then** создаются только два status-change события, `connecting` не получает
100 новых deadlines и логируется только фактическая смена behavior ID; при
CONNECTED или ERROR `connecting` завершается.

### AC-7: Первая и вторая встряска различаются (FR-7)

**Given** падение не обнаружено и история встрясок пуста  
**When** приходят две `MOTION_SHAKEN_STRONG` с разницей не более 5000 мс  
**Then** первая запускает существующее предупреждение `scared + shiver`, а
вторая запускает `angry` на 1800 мс; встряска после окна 5000 мс снова
считается первой.

### AC-8: P3 планируется и не повторяется (FR-10, FR-11, NFR-5)

**Given** Desk Mode, 90 с бездействия, фиксированный seed и оба P3-кандидата
доступны  
**When** планировщик последовательно завершает P3-активности  
**Then** полученная последовательность детерминирована, один ID не выбирается
повторно ранее 10 минут, немедленный повтор запрещён, history предпочитается
исключать, а при отсутствии кандидата остаётся базовое состояние без обхода
cooldown.

### AC-9: Пользователь отменяет P3 (FR-10, FR-12)

**Given** в Desk активен `zen` P3  
**When** приходит touch, encoder event или происходит выход из Desk  
**Then** P3 заканчивается в тот же tick, inactivity становится 0 и до 90 с
после новой активности никакое P3 не запускается.

### AC-10: Таймеры переживают wrap-around (FR-4, FR-11)

**Given** поведение стартует перед `UINT32_MAX` и заканчивается после
переполнения счётчика  
**When** `update` вызывается по обе стороны переполнения  
**Then** поведение не завершается раньше и не зависает, а cooldown вычисляется
wrap-safe.

### AC-11: Host и firmware verification проходят (FR-13, NFR-1, NFR-2, NFR-3, NFR-4, NFR-6)

**Given** чистая сборка host suite и ESP-IDF 6.0.1  
**When** запускаются `powershell -File test/host/run_tests.ps1` и
`idf.py -B build-behavior build`  
**Then** все тесты завершаются с кодом 0, firmware собирается без новых
warnings, нет новых bitmap assets/dynamic allocation, а serial monitor
показывает повторяемые переходы behavior ID при touch, наклоне, Focus и Wi‑Fi.

## Edge Cases

- EC-1: Если новое событие приходит точно в момент expiration, сначала
  завершается старое поведение, затем в том же tick обрабатывается новое.
- EC-2: Если одновременно обнаружены падение и встряска, выбирается P0
  `scared + shiver`; счётчик второй встряски не увеличивается.
- EC-3: Если наклон дрожит около порога, source adapter применяет существующий
  debounce/hysteresis и не создаёт `look_left/right` каждый sample.
- EC-4: Если Wi‑Fi остаётся CONNECTING больше 10 с, `connecting` визуально
  завершается, но новый status event не создаётся до реального перехода
  статуса.
- EC-5: Если Focus RUNNING и Wi‑Fi начинает подключение, P1 `connecting`
  временно перекрывает P2 `focused`; после завершения снова виден `focused`.
- EC-6: Если P3 interrupted пользовательским вводом, оно записывается в
  history как показанное только если было активно не меньше 1000 мс; более
  короткий показ не расходует cooldown.
- EC-7: Если seed равен 0, движок использует фиксированную ненулевую константу,
  а не недетерминированный источник в host-тестах.
- EC-8: Если MPU6050 недоступен, motion events отсутствуют, но Focus/Wi‑Fi,
  touch/encoder и P3 продолжают работать.
- EC-9: Если ширина/высота canvas равна нулю или view равен NULL, renderer
  безопасно возвращается без записи в framebuffer.
- EC-10: Если нет допустимого P3-кандидата, планировщик не зацикливается и не
  делает немедленные повторные попытки; новая попытка назначается через 60 с.

## API Contracts

Публичного HTTP API в этой итерации нет. Контракт — локальный C API, который
не включает заголовки ESP-IDF:

```c
void copet_behavior_init(copet_behavior_t *engine,
                         uint32_t now_ms,
                         uint32_t random_seed);

void copet_behavior_post(copet_behavior_t *engine,
                         const copet_behavior_event_t *event,
                         uint32_t now_ms);

void copet_behavior_update(copet_behavior_t *engine,
                           const copet_behavior_context_t *context,
                           uint32_t now_ms);

const copet_behavior_view_t *copet_behavior_get_view(
    const copet_behavior_t *engine);
```

Условный shape контракта для проверки границ модулей:

```ts
interface BehaviorEvent {
  type: "boot_completed" | "touch_short" | "encoder_left" |
        "encoder_right" | "confirm" | "motion_tilt_left" |
        "motion_tilt_right" | "motion_shaken_strong" |
        "motion_falling" | "focus_changed" | "wifi_changed" |
        "user_activity";
  value: number;
}

interface BehaviorView {
  id: string;
  priority: 0 | 1 | 2 | 3;
  elapsedMs: number;
  eyeStyle: string;
  gazeX: number;
  gazeY: number;
  groupOffsetX: number;
  groupOffsetY: number;
  overlay: string;
}
```

Source adapters в `app_main.c` отвечают только за перевод текущих enum и input
events в `copet_behavior_event_t`. Они не выбирают behavior ID самостоятельно.

## Data Models

| Field | Type | Constraints |
|---|---|---|
| `active_id` | `copet_behavior_id_t` | один из 12 ID, legacy ID или `NEUTRAL` |
| `priority` | `uint8_t` | 0–3, меньшее число важнее |
| `started_ms` | `uint32_t` | monotonic wrap-around timestamp |
| `deadline_ms` | `uint32_t` | 0 для level-triggered состояния, иначе wrap-safe deadline |
| `source` | `copet_behavior_source_t` | boot, input, motion, focus, Wi‑Fi, scheduler |
| `focus_condition` | struct | timer state + work/break phase; обновляется только при change |
| `wifi_condition` | enum | последнее `wifi_service_status_t` |
| `last_activity_ms` | `uint32_t` | обновляется touch/encoder/mode transition |
| `next_p3_ms` | `uint32_t` | первый срок после 90 с, затем 60–180 с |
| `history[4]` | ID array | четыре последних завершённых P3, newest-first |
| `last_completed_ms[]` | `uint32_t` array | cooldown timestamp на каждый P3 ID |
| `shake_window_started_ms` | `uint32_t` | окно эскалации 5000 мс |
| `random_state` | `uint32_t` | ненулевое состояние deterministic PRNG |
| `view` | `copet_behavior_view_t` | готовые параметры слоёв без ESP-IDF типов |

Renderer получает view со следующими группами данных:

| Layer | Минимальные поля | Владелец |
|---|---|---|
| background | цвет/scanlines | экран Desk или Focus |
| base eyes/mood | style, width, height, lids | behavior face renderer |
| gesture | eye-open multiplier, gaze easing | behavior face renderer |
| reaction transform | group X/Y/scale | behavior face renderer |
| action/vibe overlay | scan line, dots, dice pips | behavior face renderer |
| HUD | time, room, Focus timer, Wi‑Fi label | текущий screen renderer |

## Out of Scope

- OS-1: Bitmap/SD-анимации, Mini TV, декодирование PNG/JPEG/MJPEG и возврат
  старой gallery animation.
- OS-2: Реализация всех 103 элементов из
  `docs/20_animation_behavior_decisions.md`; после этой итерации остаются
  отдельные группы v2+.
- OS-3: P4-пасхалки, календарные события, погода как новый behavior source и
  дневно-ночное расписание.
- OS-4: Новые звуковые файлы или изменение уже утверждённых sound mappings.
- OS-5: Изменение длительности Focus 25/5, сохранения sessions или элементов
  управления Focus.
- OS-6: FreeRTOS task специально для behavior engine; движок вызывается из
  существующего неблокирующего цикла приложения.
- OS-7: Изменение распиновки, драйверов MPU6050/Wi‑Fi или сетевых credentials.
