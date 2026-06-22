"""Надёжный сброс C6 в приложение + показ состояния кнопки D1 (коды 5=нажата, 6=отпущена)."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.1
s.dtr = False; s.rts = False
s.open()

def set_rts(state):
    s.setRTS(state); s.setDTR(s.dtr)

def hard_reset():
    set_rts(True); time.sleep(0.2); set_rts(False); time.sleep(0.3)

# несколько сбросов, пока не пойдут байты
got = False
for attempt in range(1, 7):
    hard_reset(); s.reset_input_buffer()
    t0 = time.time()
    while time.time() - t0 < 1.5:
        if s.read(64):
            got = True; break
    if got:
        print(f"[{attempt}] приложение запущено, читаю кнопку.", flush=True)
        break
    print(f"[{attempt}] тихо, повтор сброса...", flush=True)
if not got:
    print(">>> Приложение не отвечает совсем (проблема не в кнопке).", flush=True)

print("\nЖМИ И ОТПУСКАЙ кнопку D1 (10 сек). Должно меняться НАЖАТА/отпущена:\n", flush=True)
buf = bytearray()
last_state = None
t0 = time.time()
while time.time() - t0 < 12:
    data = s.read(256)
    if data:
        buf.extend(data)
    while len(buf) >= 4:
        if buf[0] != 0xA5 or buf[1] != 0x5A:
            buf.pop(0); continue
        if buf[2] == 0x02:
            code = buf[3]; del buf[:4]
            if code == 7:
                if last_state != 'beacon':
                    print("  (связь есть — маяк 7)", flush=True)
                    last_state = 'beacon'
                continue
            state = 'НАЖАТА' if code == 5 else ('отпущена' if code == 6 else f'?{code}')
            if state != last_state:
                print(f"  D1: {state}", flush=True)
                last_state = state
        else:
            buf.pop(0)
s.close()
print("\nГотово.", flush=True)
