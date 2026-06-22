"""USB: сброс C6, собираем ВЕСЬ звук 8 c (без кнопки), WAV + RMS + whisper."""
import serial, time, sys, wave
import numpy as np

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
s = serial.Serial(); s.port=PORT; s.baudrate=921600; s.timeout=0.1; s.dtr=False; s.rts=False
s.open()
def set_rts(st): s.setRTS(st); s.setDTR(s.dtr)
set_rts(True); time.sleep(0.2); set_rts(False); time.sleep(0.4)
s.reset_input_buffer()
print("ГОВОРИ ГРОМКО близко к микрофону 8 секунд...", flush=True)

buf=bytearray(); pcm=bytearray()
t0=time.time()
while time.time()-t0<8:
    d=s.read(8192)
    if d: buf.extend(d)
    while len(buf)>=5:
        if buf[0]!=0xA5 or buf[1]!=0x5A: buf.pop(0); continue
        t=buf[2]
        if t==0x02: del buf[:4]; continue
        if t==0x01:
            ln=buf[3]|(buf[4]<<8)
            if len(buf)<5+ln: break
            pcm.extend(bytes(buf[5:5+ln])); del buf[:5+ln]
        else: buf.pop(0)
s.close()

print(f"собрано {len(pcm)} байт ({len(pcm)/2/16000:.1f} c аудио)", flush=True)
if len(pcm)<4000:
    print(">>> Аудио почти нет — ESP не шлёт поток (сброс ушёл в загрузку? проверь USB).",flush=True); sys.exit(0)

x=np.frombuffer(bytes(pcm),dtype=np.int16).astype(np.float32); xc=x-x.mean()
rms=np.sqrt(np.mean((xc/32768)**2)); peak=np.max(np.abs(xc))/32768
print(f"RMS={rms:.4f} пик={peak:.4f} (речь норм RMS~0.02-0.2)", flush=True)
with wave.open(r"D:\test\esp_usb_record.wav","wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(bytes(pcm))
try:
    from faster_whisper import WhisperModel
    m=WhisperModel('base',device='cpu',compute_type='int8')
    segs,info=m.transcribe(xc.astype(np.float32)/32768.0,language='ru',beam_size=1,vad_filter=True)
    txt=''.join(seg.text for seg in segs).strip()
    open(r'D:\test\_usb_asr.txt','w',encoding='utf-8').write(f"whisper: {txt!r}")
    print("whisper -> _usb_asr.txt", flush=True)
except Exception as e:
    print("whisper skip:",e,flush=True)
