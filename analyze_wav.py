"""Анализ last_record.wav: громкость, DC, шум, спектр, наводки."""
import sys, wave
import numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else r"D:\test\last_record.wav"
w = wave.open(path, "rb")
sr = w.getframerate(); n = w.getnframes(); ch = w.getnchannels()
raw = w.readframes(n); w.close()
x = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
if ch > 1:
    x = x[::ch]
xn = x / 32768.0

print(f"файл: {path}")
print(f"sr={sr} Гц, сэмплов={len(xn)}, длительность={len(xn)/sr:.2f} c, каналов={ch}")
print(f"DC(среднее)={np.mean(xn):+.4f}")
xc = xn - np.mean(xn)
print(f"RMS={np.sqrt(np.mean(xc**2)):.4f}  пик={np.max(np.abs(xc)):.4f}")
print(f"клиппинг(|s|>0.98)={np.mean(np.abs(xn)>0.98)*100:.2f}%")
# битность: сколько уникальных уровней (грубая оценка реального разрешения)
uniq = len(np.unique(np.round(x).astype(np.int32)))
print(f"уникальных уровней={uniq} (мало -> слабый сигнал/шум квантования)")

# Спектр: доля энергии в речевой полосе vs остальное
X = np.abs(np.fft.rfft(xc * np.hanning(len(xc))))
f = np.fft.rfftfreq(len(xc), 1/sr)
def band(lo, hi):
    m = (f >= lo) & (f < hi)
    return float(np.sum(X[m]**2))
tot = float(np.sum(X**2)) + 1e-9
print("\nраспределение энергии по полосам:")
for lo, hi in [(0,80),(80,300),(300,1000),(1000,3000),(3000,6000),(6000,8000)]:
    print(f"  {lo:>4}-{hi:<4} Гц: {band(lo,hi)/tot*100:5.1f}%")

# Пики спектра (наводки: 50/60 Гц и гармоники, или одиночная частота)
top = np.argsort(X)[-8:][::-1]
print("\nтоп-8 частотных пиков (Гц : амплитуда):")
for i in top:
    print(f"  {f[i]:7.1f} Гц : {X[i]:.1f}")
