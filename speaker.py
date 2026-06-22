#!/usr/bin/env python3
"""
Получает PCM-аудио от ESP32 по serial и играет через колонки ПК.

Установка зависимостей:
  pip install pyserial sounddevice numpy

Запуск:
  python speaker.py          # использует COM11 по умолчанию
  python speaker.py COM5     # указать другой порт
"""

import sys
import time
import struct
import threading
import queue

import serial
import numpy as np
import sounddevice as sd

# ── настройки ──────────────────────────────────────────────
COM_PORT    = sys.argv[1] if len(sys.argv) > 1 else 'COM11'
BAUD_RATE   = 921600
SAMPLE_RATE = 24000   # должно совпадать с AUDIO_OUTPUT_SAMPLE_RATE в прошивке
BLOCK_SIZE  = 240     # сэмплов на блок (10 мс при 24 кГц)
# ───────────────────────────────────────────────────────────

audio_queue: queue.Queue = queue.Queue(maxsize=40)


def audio_callback(outdata, frames, time_info, status):
    """sounddevice вызывает эту функцию каждые BLOCK_SIZE сэмплов."""
    try:
        chunk = audio_queue.get_nowait()
        n = min(len(chunk), frames)
        outdata[:n] = chunk[:n].reshape(n, 1)
        if n < frames:
            outdata[n:] = 0
    except queue.Empty:
        outdata[:] = 0


def serial_reader():
    """Читает байты с serial, находит фреймы [0xAA 0x55 len_lo len_hi data...] и кладёт в очередь."""
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.5)
        print(f"[serial] подключено к {COM_PORT} @ {BAUD_RATE} бод")
    except serial.SerialException as e:
        print(f"[serial] ОШИБКА открытия порта: {e}")
        return

    buf = bytearray()
    frames_received = 0

    while True:
        try:
            chunk = ser.read(1024)
            if not chunk:
                continue
            buf.extend(chunk)

            # Ищем фреймы в буфере
            while len(buf) >= 4:
                # Ищем заголовок 0xAA 0x55
                idx = -1
                for i in range(len(buf) - 1):
                    if buf[i] == 0xAA and buf[i + 1] == 0x55:
                        idx = i
                        break

                if idx == -1:
                    # Заголовка нет — это текст лога, печатаем и выбрасываем
                    text_end = len(buf) - 1
                    try:
                        text = buf[:text_end].decode('utf-8', errors='replace')
                        if text.strip():
                            print(f"[esp32] {text}", end='')
                    except Exception:
                        pass
                    buf = buf[-1:]
                    break

                # Есть текст перед заголовком — печатаем его
                if idx > 0:
                    try:
                        text = buf[:idx].decode('utf-8', errors='replace')
                        if text.strip():
                            print(f"[esp32] {text}", end='')
                    except Exception:
                        pass
                    buf = buf[idx:]

                if len(buf) < 4:
                    break

                # Читаем длину данных
                length = struct.unpack_from('<H', buf, 2)[0]

                if length == 0 or length > 8192:
                    # Некорректная длина — пропускаем этот заголовок
                    buf = buf[2:]
                    continue

                # Ждём пока придут все данные фрейма
                if len(buf) < 4 + length:
                    break

                # Извлекаем PCM
                pcm_bytes = bytes(buf[4:4 + length])
                buf = buf[4 + length:]

                samples = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0

                try:
                    audio_queue.put_nowait(samples)
                    frames_received += 1
                    if frames_received % 100 == 0:
                        print(f"[audio] получено фреймов: {frames_received}, очередь: {audio_queue.qsize()}")
                except queue.Full:
                    # Буфер переполнен — пропускаем старый фрейм
                    try:
                        audio_queue.get_nowait()
                        audio_queue.put_nowait(samples)
                    except Exception:
                        pass

        except serial.SerialException as e:
            print(f"[serial] разрыв соединения: {e}")
            break
        except Exception as e:
            print(f"[serial] ошибка: {e}")
            time.sleep(0.1)


if __name__ == '__main__':
    print("=" * 50)
    print("ESP32 → ПК аудиомост")
    print(f"  Порт:        {COM_PORT}")
    print(f"  Baud:        {BAUD_RATE}")
    print(f"  Sample rate: {SAMPLE_RATE} Гц")
    print("=" * 50)
    print("Ctrl+C для выхода\n")

    # Запускаем поток чтения serial
    t = threading.Thread(target=serial_reader, daemon=True)
    t.start()

    # Небольшая пауза чтобы serial успел подключиться
    time.sleep(1.0)

    # Запускаем аудиовыход
    try:
        with sd.OutputStream(samplerate=SAMPLE_RATE,
                              channels=1,
                              dtype='float32',
                              blocksize=BLOCK_SIZE,
                              callback=audio_callback):
            print("[audio] поток запущен, ждём данных от ESP32...")
            while t.is_alive():
                time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[выход]")
    except Exception as e:
        print(f"[audio] ошибка: {e}")
        print("Убедись что установлены: pip install pyserial sounddevice numpy")
