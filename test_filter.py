"""Прогон whisper на last_record.wav: без фильтра и с новым речевым фильтром."""
import wave, numpy as np
from scipy.signal import butter, sosfilt
from faster_whisper import WhisperModel

w = wave.open(r"D:\test\last_record.wav", "rb")
sr = w.getframerate(); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)/32768.0
w.close()

def filt(x):
    sos = butter(4, [150.0, min(7000.0, sr/2-200)], btype='band', fs=sr, output='sos')
    y = sosfilt(sos, x - np.mean(x)).astype(np.float32)
    y = np.concatenate(([y[0]], y[1:] - 0.95*y[:-1])).astype(np.float32)
    # нормализация
    r = np.sqrt(np.mean(y**2)) + 1e-9
    y = np.clip(y * min(0.08/r, 30.0), -0.95, 0.95)
    return y.astype(np.float32)

m = WhisperModel("base", device="cpu", compute_type="int8")
for name, sig in [("БЕЗ фильтра", x), ("С фильтром", filt(x))]:
    segs, info = m.transcribe(sig, language="ru", beam_size=1, vad_filter=True)
    txt = "".join(s.text for s in segs).strip()
    print(f"[{name}] язык={info.language} текст={txt!r}", flush=True)
# сохраним отфильтрованный для прослушивания
yf = filt(x)
with wave.open(r"D:\test\last_record_filtered.wav","wb") as o:
    o.setnchannels(1); o.setsampwidth(2); o.setframerate(sr)
    o.writeframes(np.clip(yf*32768,-32768,32767).astype(np.int16).tobytes())
print("сохранён last_record_filtered.wav", flush=True)
