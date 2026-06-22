"""Сброс C6 в приложение и печать ВСЕГО вывода как текст (ловим панику/краш)."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()

def set_rts(state):
    s.setRTS(state); s.setDTR(s.dtr)

set_rts(True); time.sleep(0.2); set_rts(False); time.sleep(0.3)
s.reset_input_buffer()

raw = bytearray()
t0 = time.time()
while time.time() - t0 < 6:
    d = s.read(4096)
    if d: raw.extend(d)
s.close()

# Выкидываем наши бинарные кадры-пульсы (A5 5A 02 07), печатаем остальной текст
text = bytes(b if (9 <= b <= 13 or 32 <= b < 127) else 0x2e for b in raw)
print(text.decode('ascii', 'replace'), flush=True)
