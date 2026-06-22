"""Находит ESP по UDP-discovery, цепляется к TCP 3333, ловит запись по кнопке,
сохраняет WAV + меряет громкость. Так проверяем аудио ESP по Wi-Fi (как у телефона)."""
import socket, struct, time, sys, wave
import numpy as np

# 1) UDP discovery
def discover(timeout=3.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(timeout)
    s.sendto(b"ESP32C6_MIC?", ("255.255.255.255", 3334))
    try:
        data, addr = s.recvfrom(256)
        msg = data.decode("utf-8", "replace")
        print("discovery ответ:", msg)
        ip = None
        for part in msg.split(";"):
            if part.startswith("ip="):
                ip = part[3:]
        return ip
    except socket.timeout:
        return None
    finally:
        s.close()

ip = sys.argv[1] if len(sys.argv) > 1 else discover()
if not ip:
    print(">>> ESP не найдена по discovery. ПК и ESP в одной Wi-Fi? Укажи IP: python wifi_test.py 192.168.0.XX")
    sys.exit(1)
print("ESP IP:", ip)

# 2) TCP connect + parse
sock = socket.create_connection((ip, 3333), timeout=5)
sock.settimeout(0.5)
print("TCP подключён. Нажми кнопку D1, говори, нажми ещё раз (или Ctrl+C).")

buf = bytearray(); rec = bytearray(); recording = False; got = False
MAG0, MAG1 = 0xA5, 0x5A
t0 = time.time()
while time.time() - t0 < 30 and not got:
    try:
        chunk = sock.recv(8192)
    except socket.timeout:
        continue
    if not chunk:
        break
    buf.extend(chunk)
    while len(buf) >= 4:
        if buf[0] != MAG0 or buf[1] != MAG1:
            buf.pop(0); continue
        t = buf[2]
        if t == 0x02:
            code = buf[3]; del buf[:4]
            if code == 1:
                recording = True; rec = bytearray(); print("[esp] СТАРТ записи")
            elif code == 0:
                recording = False; print(f"[esp] СТОП, собрано {len(rec)} байт")
                got = True
        elif t == 0x01:
            if len(buf) < 5: break
            ln = buf[3] | (buf[4] << 8)
            if len(buf) < 5 + ln: break
            payload = bytes(buf[5:5+ln]); del buf[:5+ln]
            if recording: rec.extend(payload)
        else:
            buf.pop(0)
sock.close()

if len(rec) < 100:
    print(">>> Записи нет (нажми кнопку во время теста). Событий старт/стоп не поймано.")
    sys.exit(0)

x = np.frombuffer(bytes(rec), dtype=np.int16).astype(np.float32)
xc = x - x.mean()
rms = np.sqrt(np.mean((xc/32768.0)**2)); peak = np.max(np.abs(xc))/32768.0
print(f"длительность={len(x)/16000:.2f}c RMS={rms:.4f} пик={peak:.4f}")
with wave.open(r"D:\test\esp_wifi_record.wav","wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(bytes(rec))
print("сохранён D:\\test\\esp_wifi_record.wav")
