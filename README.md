# CoPet Pilot

Настольный embedded-робот-компаньон на **ESP32-WROOM-32 DevKit**: экран-лицо,
Desk Mode, меню, фокус-таймер, датчики, локальные анимации, звук и прямые
Wi-Fi-функции без обязательного телефонного приложения.

Полное техническое задание — [CoPet_Pilot_general_spec.md](CoPet_Pilot_general_spec.md).

> **Статус:** рабочий аппаратный прототип. Проверены ST7789, SHT31, энкодер,
> TTP223, Focus Mode, BLE-диагностика, I2S loopback и локальная анимация.
> Behavior Engine v1 разрешает приоритеты P0–P3, получает события MPU6050,
> Focus и Wi‑Fi, планирует неповторяющиеся P3-активности и процедурно рисует
> первую группу из 12 поведений. Чистая логика проверяется host-тестами.
> Desk Mode уже является домашним экраном: процедурное лицо моргает, смотрит
> по сторонам, реагирует на касание и показывает комфорт комнаты. Следующий
> На Desk в терминальном зелёном стиле по касанию переключаются комнатные данные
> SHT31 и уличная погода через Direct Wi-Fi. Логика режимов (Desk, Menu, Focus,
> Desk, Menu, Focus и Settings) и рендер каждого активного экрана вынесены из
> `app_main.c` в модули `modes/`
> и `ui/`; переходы состояний покрыты host-тестами (`test/host/`). `app_main.c`
> теперь тонкий слой интеграции железа и главного цикла.
> Пользовательское меню в том же стиле показывает только `FOCUS` и `SETTINGS`.
> Галерейная анимация сохранена в проекте, но исключена из прошивки до SD-этапа.
> Focus Mode использует таймер 25/5, динамические START/PAUSE/RESUME-подсказки
> и сохраняет остаток времени при возврате в Desk.

## Структура

```text
main/
  app_main.c              — интеграция железа и главный цикл
  core/                   — общие идентификаторы состояний (copet_modes.h)
  modes/                  — desk / menu / focus / animation (чистая логика)
  ui/                     — ui_canvas (общие примитивы) + рендер по экранам
  drivers/                — ST7789, кнопка, энкодер, SHT31, MPU6050, I2S, SD
  services/               — Wi-Fi, погода/HTTP (далее assistant, time, media)
test/
  host/                   — host-тесты переходов состояний (без платы)
```

Логика режимов и рендер экранов вынесены из `app_main.c`. Ассистент и Mini TV
пока не реализованы — их модули появятся при работе над M7/M8. Оставшиеся
переносы (например, вынос дисплей-драйвера из `app_main.c`) продолжаются
небольшими проверяемыми шагами.

Соответствие модулей режимам и железу описано в разделах 4–5 и 14 ТЗ.

## Аппаратная платформа (MVP)

- ESP32-WROOM-32 DevKit
- ST7789 240×240 TFT (SPI)
- трёхконтактное колесо от мыши + TTP223
- SHT31 (I2C), MPU6050 (I2C)
- INMP441 + MAX98357A + динамик (I2S)
- Wi-Fi 2.4 GHz

SD/Mini TV добавляются после стабильной базовой архитектуры. GPS/GNSS и
Outdoor Mode отложены до следующей версии.

## Сборка

Активная прошивка собирается с **ESP-IDF 6.0.1** для target `esp32`:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash monitor
```

Для первого теста Direct Wi-Fi открой локальную конфигурацию:

```powershell
idf.py menuconfig
```

Затем выбери `CoPet Pilot → Wi-Fi SSID` и `Wi-Fi password`, сохрани настройки
и снова выполни `idf.py build`. Пароль попадает только в локальный `sdkconfig`,
который исключён из Git. Без настроек Desk Mode продолжает работать и показывает
`WIFI SET`; при успешном подключении — `WIFI OK`.

Погода включена по умолчанию для Самары. Координаты меняются там же, в
`CoPet Pilot → Weather latitude` и `Weather longitude`. После загрузки карточки
`TEMP` и `HUM` показывают комнатные значения `ROOM` от SHT31. Короткое касание
переключает их на уличные значения `OUT` и обратно, не отменяя реакцию лица.
Подробности и результат проверки на плате: [`docs/21_wifi_station_learning_log.md`](docs/21_wifi_station_learning_log.md).

Номер COM-порта может отличаться. Полная архитектура описана в
[`docs/01_architecture.md`](docs/01_architecture.md), принятые решения — в
[`docs/architecture/`](docs/architecture/), экранные сценарии и wireframe — в
[`docs/ux/01_user_cases_and_screen_flow.md`](docs/ux/01_user_cases_and_screen_flow.md).

## Вдохновение и благодарности

Идея процедурных «живых глаз» для Desk Mode вдохновлена проектом
[esp-bridge-mcp-robot](https://github.com/HamzaYslmn/esp-bridge-mcp-robot).
Выбранные механики эмоций, жестов и анимации `smoking` портированы на C и
адаптированы для цветного ST7789. Это модифицированная производная реализация,
а не оригинальная версия Pip. Полный текст применимой лицензии сохранён в
[`THIRD_PARTY_LICENSES/esp-bridge-mcp-robot-LICENSE.txt`](THIRD_PARTY_LICENSES/esp-bridge-mcp-robot-LICENSE.txt),
а атрибуция и ссылки доступны в этом репозитории.

Originally created by Hamza Yeşilmen (HamzaYslmn).
Source: https://github.com/HamzaYslmn/
Sponsor: https://github.com/sponsors/HamzaYslmn
