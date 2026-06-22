"""Надёжный сброс C6 (USB-JTAG) В ПРИЛОЖЕНИЕ (как esptool, с пинком DTR) + чтение, с повторами."""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial()
s.port = PORT; s.baudrate = 921600; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()

def set_rts(state):
    s.setRTS(state)
    s.setDTR(s.dtr)   # Windows: пнуть DTR, чтобы управляющий запрос реально ушёл

def hard_reset_usb():
    # как esptool HardReset(uses_usb=True): EN->LOW, пауза, EN->HIGH
    set_rts(True)
    time.sleep(0.2)
    set_rts(False)
    time.sleep(0.3)

for attempt in range(1, 7):
    print(f"[{attempt}] сброс C6 в приложение...", flush=True)
    s.dtr = False
    hard_reset_usb()
    s.reset_input_buffer()
    total = 0; ev7=ev8=ev9=audio=junk=0; ev5=ev6=ev0=ev1=0; lens=set()
    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < 9:
        data = s.read(4096)
        if not data:
            continue
        total += len(data)
        buf.extend(data)
        while len(buf) >= 4:
            if buf[0] != 0xA5 or buf[1] != 0x5A:
                buf.pop(0); junk += 1; continue
            if buf[2] == 0x02:
                c = buf[3]; del buf[:4]
                if c==7: ev7+=1
                elif c==8: ev8+=1
                elif c==9: ev9+=1
                elif c==5: ev5+=1
                elif c==6: ev6+=1
                elif c==0: ev0+=1
                elif c==1: ev1+=1
            elif buf[2] == 0x01:
                if len(buf) < 5: break
                ln = buf[3] | (buf[4] << 8)
                if len(buf) < 5 + ln: break
                del buf[:5+ln]; audio+=1; lens.add(ln)
            else:
                buf.pop(0); junk += 1
    if total > 0:
        print(f"    байт={total} ev7={ev7} ev8={ev8} ev9={ev9} audio={audio} junk={junk} | КНОПКА: нажатий(0/1)={ev0+ev1}", flush=True)
        if ev0 + ev1 > 0:
            print(">>> КНОПКА D1 РАБОТАЕТ! Зарегистрировано нажатий:", ev0 + ev1, flush=True)
        else:
            print(">>> Связь/звук есть, но нажатий D1 НЕ видно (если ты жала — провод D1 не на GND / не та кнопка).", flush=True)
        if audio > 0 and junk < audio:
            print(">>> Аудио идёт и кадры синхронны (перевод строк НЕ бьёт). Можно работать!", flush=True)
        elif junk > audio:
            print(">>> Аудио ПРИХОДИТ, но кадры рассинхронены (0x0A портит поток). Нужна защита от \\n.", flush=True)
        break
    else:
        print("    тихо, повтор...", flush=True)
s.close()
