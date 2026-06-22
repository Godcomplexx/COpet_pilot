"""Сброс C6 в приложение и вывод только строк лога про I2C/OLED (аудио-байты отфильтровываем)."""
import serial, sys, time, re

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial(); s.port=PORT; s.baudrate=921600; s.timeout=0.1; s.dtr=False; s.rts=False
s.open()
def set_rts(st): s.setRTS(st); s.setDTR(s.dtr)
set_rts(True); time.sleep(0.2); set_rts(False); time.sleep(0.3)
s.reset_input_buffer()

raw = bytearray()
t0 = time.time()
while time.time() - t0 < 5:
    d = s.read(8192)
    if d: raw.extend(d)
s.close()

# Достаём печатаемые "слова"/строки, ищем ключевые
text = ''.join(chr(b) if (9 <= b <= 13 or 32 <= b < 127) else '\n' for b in raw)
keys = ('oled', 'I2C', 'SSD', 'found', 'scan', 'OLED', 'eyes', 'device', 'SDA', 'glasses')
seen = set()
for line in text.split('\n'):
    line = line.strip()
    if len(line) >= 4 and any(k.lower() in line.lower() for k in keys):
        if line not in seen:
            seen.add(line)
            print(line, flush=True)
if not seen:
    print(">>> Ничего про I2C/OLED в логе (возможно, уровень логов WARN скрывает INFO).", flush=True)
