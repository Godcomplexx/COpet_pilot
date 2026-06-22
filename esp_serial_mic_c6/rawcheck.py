"""Простейшая проверка: приходит ли ВООБЩЕ что-то на порт. Печатает счётчик раз в секунду."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()
s.dtr = False; s.rts = False
print(f"Слушаю {PORT}. Нажимай D1 / говори в микрофон. (Ctrl+C — выход)\n", flush=True)

total = 0
last = time.time()
while True:
    data = s.read(4096)
    if data:
        total += len(data)
        print(f"  +{len(data)} байт | hex: {data[:24].hex(' ')}", flush=True)
    if time.time() - last >= 1.0:
        last = time.time()
        print(f"--- всего получено: {total} байт ---", flush=True)
