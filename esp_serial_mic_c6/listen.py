"""Сырой монитор COM-порта: показывает всё, что шлёт ESP (логи + наши кадры)."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.3
s.dtr = True; s.rts = True   # для USB-CDC иногда нужно «терминал открыт»
s.open()
print(f"Слушаю {PORT}. Нажимай кнопку D1, говори в микрофон. Ctrl+C для выхода.\n", flush=True)

audio_frames = 0
events = 0
other = 0
buf = bytearray()
t0 = time.time()
while True:
    try:
        data = s.read(4096)
    except KeyboardInterrupt:
        break
    if not data:
        continue
    buf.extend(data)
    # парсим кадры 0xA5 0x5A
    while len(buf) >= 4:
        if buf[0] != 0xA5 or buf[1] != 0x5A:
            other += 1
            # печатаем как текст (это лог чипа)
            sys.stdout.write(chr(buf[0]) if 32 <= buf[0] < 127 else '.')
            buf.pop(0)
            continue
        t = buf[2]
        if t == 0x02:
            if len(buf) < 4: break
            code = buf[3]; del buf[:4]; events += 1
            print(f"\n[EVENT] code={code} (1=старт,0=стоп)  всего событий={events}", flush=True)
        elif t == 0x01:
            if len(buf) < 5: break
            ln = buf[3] | (buf[4] << 8)
            if len(buf) < 5 + ln: break
            del buf[:5 + ln]; audio_frames += 1
            if audio_frames % 20 == 0:
                print(f"\n[AUDIO] кадров={audio_frames} (по {ln} б)", flush=True)
        else:
            buf.pop(0)
    if time.time() - t0 > 1.5:
        t0 = time.time()
        print(f"\n--- статус: audio={audio_frames} events={events} мусор/лог={other} ---", flush=True)
