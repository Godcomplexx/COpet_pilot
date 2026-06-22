"""Пассивный ридер (БЕЗ сбросов): открыть порт и читать состояние кнопки D1."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.1
s.dtr = False; s.rts = False
s.open()
s.dtr = False; s.rts = False
print("Открыл порт без сброса. ЖМИ/ОТПУСКАЙ D1 (15 сек)...\n", flush=True)

buf = bytearray()
last_state = None
total = 0
t0 = time.time()
while time.time() - t0 < 15:
    data = s.read(256)
    if data:
        total += len(data); buf.extend(data)
    while len(buf) >= 4:
        if buf[0] != 0xA5 or buf[1] != 0x5A:
            buf.pop(0); continue
        if buf[2] == 0x02:
            code = buf[3]; del buf[:4]
            state = 'НАЖАТА' if code == 5 else ('отпущена' if code == 6 else f'код{code}')
            if state != last_state:
                print(f"  D1: {state}", flush=True)
                last_state = state
        else:
            buf.pop(0)
s.close()
print(f"\nВсего байт: {total}", flush=True)
if total == 0:
    print(">>> Тишина: приложение не шлёт. Нужен чистый старт (переткни USB) или сброс.", flush=True)
