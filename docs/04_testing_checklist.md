# 04. Testing Checklist

## Global checklist

| Module | Ready when |
|---|---|
| ESP32 base | boots and logs current mode |
| ST7789 | shows text and simple face |
| Menu | selection changes via encoder |
| Touch | short/long press detected |
| SHT31 | temp/humidity update correctly |
| MPU6050 | tilt/motion state detected |
| Audio | test sound plays |
| SD | file list is readable |
| Mini TV | one local clip plays |
| BLE | phone sends JSON to ESP32 |
| GPS | speed shown when fix exists |

## Debug discipline

For every module log:

```text
[module] init start
[module] detected / not detected
[module] config values
[module] first valid reading
[module] error code if failed
```

## Do not continue if

- wires are not documented;
- pin map is not updated;
- module has no minimal test;
- error is ignored;
- behavior is “sometimes works”.
