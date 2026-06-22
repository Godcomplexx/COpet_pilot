import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
print(f"Мониторинг {PORT} на 921600 бод (Ctrl+C выход)")

ser = serial.Serial(PORT, 921600, timeout=0.5)
buf = b''
while True:
    try:
        d = ser.read(512)
        if not d:
            continue
        buf += d
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            try:
                text = line.decode('utf-8', errors='replace').strip()
                if text and not text.startswith('\x00'):
                    print(text)
            except:
                pass
    except KeyboardInterrupt:
        break
ser.close()
