# CoPet Pilot

Настольный embedded-робот-компаньон на **ESP32-WROOM-32 DevKit**: экран-лицо,
Desk Mode, меню, фокус-таймер, датчики, локальные анимации, звук и прямые
Wi-Fi/HTTPS-функции без обязательного телефонного приложения.

Полное техническое задание — [CoPet_Pilot_general_spec.md](CoPet_Pilot_general_spec.md).

> **Статус:** рабочий аппаратный прототип. Проверены ST7789, SHT31, энкодер,
> TTP223, Focus Mode, BLE-диагностика, I2S loopback и встроенная анимация.
> Desk Mode уже является домашним экраном: процедурное лицо моргает, смотрит
> по сторонам, реагирует на касание и показывает комфорт комнаты. Следующий
> этап — продолжение модульного рефакторинга и Direct Wi-Fi.

## Структура

```text
main/
  app_main.c              — точка входа
  core/                   — state machine, события, конфиг
  modes/                  — Desk, Menu, Focus, Assistant, Animation, Settings
  ui/                     — framebuffer, primitives, face, menu, cards
  drivers/                — ST7789, кнопка, энкодер, SHT31, MPU6050, I2S, SD
  services/               — sensors, focus, Wi-Fi, HTTP, assistant, time, media
```

Это целевая структура. Сейчас часть модулей ещё находится в `app_main.c` и
будет переноситься небольшими проверяемыми шагами.

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

Активная прошивка собирается с **ESP-IDF 5.5.2** для target `esp32`:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash monitor
```

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
атрибуция и sponsor link также доступны на экране `CREDITS` устройства.

Originally created by Hamza Yeşilmen (HamzaYslmn).
Source: https://github.com/HamzaYslmn/
Sponsor: https://github.com/sponsors/HamzaYslmn
