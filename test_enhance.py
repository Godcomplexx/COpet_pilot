"""Точная копия Kotlin enhanceForAsr — проверяем, не зануляет ли фильтр сигнал."""
import numpy as np, glob, os, wave

def enhance(pcm_i16):
    n = len(pcm_i16)
    if n < 16: return pcm_i16
    x = pcm_i16.astype(np.float32)
    x -= x.mean()                                  # DC removal
    # ФВЧ: y=a*(y_prev + x - x_prev)
    a=0.95; py=0.0; px=0.0; y=np.empty(n,np.float32)
    for i in range(n):
        cur=x[i]; out=a*(py+cur-px); py=out; px=cur; y[i]=out
    x=y
    # предыскажение
    pe=0.0
    for i in range(n):
        cur=x[i]; x[i]=cur-0.95*pe; pe=cur
    peak=max(1.0, float(np.max(np.abs(x))))
    gain=min(0.6*32768/peak, 30.0)
    out=np.clip(x*gain,-32768,32767).astype(np.int16)
    return out

def rms(a): a=a.astype(np.float32); return float(np.sqrt(np.mean((a/32768)**2)))

# 1) синтетический тон 300+1500 Гц (как речь) — фильтр НЕ должен его убить
sr=16000; t=np.arange(sr)/sr
tone=((np.sin(2*np.pi*300*t)+0.5*np.sin(2*np.pi*1500*t))*3000).astype(np.int16)
o=enhance(tone)
print(f"[тон 300+1500Гц] вход RMS={rms(tone):.4f} -> выход RMS={rms(o):.4f}")

# 2) тихий низкочастотный сигнал (как аналоговый мик: только бас+шум)
low=((np.sin(2*np.pi*120*t)*80)+np.random.randint(-20,20,sr)).astype(np.int16)
o2=enhance(low)
print(f"[низ 120Гц тихий] вход RMS={rms(low):.4f} -> выход RMS={rms(o2):.4f}")

# 3) реальные WAV на ПК
for f in glob.glob(r"D:\test\*.wav"):
    try:
        w=wave.open(f,'rb'); d=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16); w.close()
        if len(d)<16: print(f"[{os.path.basename(f)}] пусто ({len(d)} сэмплов)"); continue
        o=enhance(d)
        print(f"[{os.path.basename(f)}] вход RMS={rms(d):.4f} пик={np.max(np.abs(d))} -> после фильтра RMS={rms(o):.4f}")
    except Exception as e:
        print(f"[{f}] ошибка: {e}")
