# 00. Learning Strategy

## Цель

Сделать CoPet Pilot учебным embedded-проектом, где каждый модуль изучается через понимание интерфейсов, datasheet-level reasoning и маленькие эксперименты.

## Не copy-paste подход

Плохой путь:

```text
найти готовый проект → скопировать → поменять пины → не понимать, почему работает
```

Правильный путь:

```text
найти пример → понять интерфейс → переписать минимальный код → проверить → интегрировать
```

## Метод одного модуля

На каждый модуль:

| Шаг | Что сделать |
|---|---|
| 1 | понять интерфейс |
| 2 | найти datasheet / pinout |
| 3 | сделать минимальный тест |
| 4 | написать wrapper |
| 5 | добавить в state machine |
| 6 | записать выводы |

## Вопросы перед кодом

Перед тем как писать драйвер, ответь:

1. Какой интерфейс у модуля: SPI, I2C, UART, GPIO, I2S?
2. Какие пины нужны?
3. Кто master, кто slave?
4. Какой адрес/скорость/частота?
5. Что является минимальным успешным результатом?
6. Как понять, что модуль не работает?
7. Какие ошибки наиболее вероятны?

## Формат учебной записи

```md
## Module: ST7789

Interface: SPI  
Goal: show text on screen  
Minimal result: "CoPet Boot OK" visible  
What I learned:
- DC pin separates command/data
- RST resets controller
- BLK controls backlight

Problems:
- white screen = wrong init/backlight
- wrong colors = RGB/BGR order issue
```
