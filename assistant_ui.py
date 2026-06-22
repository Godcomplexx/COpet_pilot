#!/usr/bin/env python3
import concurrent.futures
import ipaddress
import queue
import socket
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk

import numpy as np

import assistant

try:
    import sounddevice as sd
except Exception:
    sd = None

try:
    import winsound
except Exception:
    winsound = None


class AssistantUI:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("ESP32 Voice Assistant")
        self.root.geometry("980x680")
        self.root.minsize(820, 560)

        self.ui_queue: "queue.Queue[tuple[str, object]]" = queue.Queue()
        self.audio_queue: "queue.Queue[tuple[str, np.ndarray]]" = queue.Queue()
        self.stop_event = threading.Event()

        self.started = False
        self.discovering = False
        self.recording_active = False
        self.pc_recording = False
        self.pc_chunks: list[np.ndarray] = []
        self.pc_stream = None

        self.transport_var = tk.StringVar(value="Serial")
        self.port_var = tk.StringVar(value="COM5")
        self.host_var = tk.StringVar(value="192.168.4.1")
        self.tcp_port_var = tk.StringVar(value="3333")
        self.serial_status_var = tk.StringVar(value="Serial: stopped")
        self.brain_status_var = tk.StringVar(value="Brain: stopped")
        self.audio_status_var = tk.StringVar(value="Audio: idle")
        self.recording_hint_var = tk.StringVar(value="")
        self.last_text_var = tk.StringVar(value="")
        self.last_answer_var = tk.StringVar(value="")

        self._build_ui()
        self.root.after(80, self._poll_ui_queue)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(3, weight=1)

        top = ttk.Frame(self.root, padding=(12, 12, 12, 8))
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(10, weight=1)

        ttk.Label(top, text="Mode").grid(row=0, column=0, padx=(0, 6))
        self.transport_box = ttk.Combobox(
            top,
            textvariable=self.transport_var,
            values=("Serial", "WiFi"),
            state="readonly",
            width=8,
        )
        self.transport_box.grid(row=0, column=1, padx=(0, 10))

        ttk.Label(top, text="Port").grid(row=0, column=2, padx=(0, 6))
        self.port_entry = ttk.Entry(top, textvariable=self.port_var, width=10)
        self.port_entry.grid(row=0, column=3, padx=(0, 10))

        ttk.Label(top, text="Host").grid(row=0, column=4, padx=(0, 6))
        self.host_entry = ttk.Entry(top, textvariable=self.host_var, width=14)
        self.host_entry.grid(row=0, column=5, padx=(0, 10))

        ttk.Label(top, text="TCP").grid(row=0, column=6, padx=(0, 6))
        self.tcp_port_entry = ttk.Entry(top, textvariable=self.tcp_port_var, width=7)
        self.tcp_port_entry.grid(row=0, column=7, padx=(0, 10))

        self.discover_button = ttk.Button(top, text="Discover", command=self.discover_wifi_esp)
        self.discover_button.grid(row=0, column=8, padx=(0, 8))

        self.start_button = ttk.Button(top, text="Start", command=self.start)
        self.start_button.grid(row=0, column=9, padx=(0, 8))

        self.pc_button = ttk.Button(
            top, text="PC mic", command=self.toggle_pc_recording, state="disabled"
        )
        self.pc_button.grid(row=0, column=10, padx=(0, 8), sticky="w")

        self.clear_button = ttk.Button(top, text="Clear log", command=self.clear_log)
        self.clear_button.grid(row=0, column=11, padx=(0, 12))

        status = ttk.Frame(self.root, padding=(12, 0, 12, 8))
        status.grid(row=1, column=0, sticky="ew")
        status.columnconfigure(0, weight=1)
        status.columnconfigure(1, weight=1)
        status.columnconfigure(2, weight=1)

        ttk.Label(status, textvariable=self.serial_status_var).grid(
            row=0, column=0, sticky="w"
        )
        ttk.Label(status, textvariable=self.brain_status_var).grid(
            row=0, column=1, sticky="w"
        )
        ttk.Label(status, textvariable=self.audio_status_var).grid(
            row=0, column=2, sticky="w"
        )

        self.recording_hint = tk.Label(
            self.root,
            textvariable=self.recording_hint_var,
            height=2,
            bg="#eceff3",
            fg="#5b6470",
            font=("Segoe UI", 18, "bold"),
        )
        self.recording_hint.grid(row=2, column=0, sticky="ew", padx=12, pady=(0, 8))

        body = ttk.PanedWindow(self.root, orient=tk.VERTICAL)
        body.grid(row=3, column=0, sticky="nsew", padx=12, pady=(0, 12))

        conversation = ttk.Frame(body, padding=0)
        conversation.columnconfigure(0, weight=1)
        conversation.rowconfigure(1, weight=1)
        conversation.rowconfigure(3, weight=1)

        ttk.Label(conversation, text="Recognized text").grid(
            row=0, column=0, sticky="w", pady=(0, 4)
        )
        self.text_box = tk.Text(
            conversation,
            height=4,
            wrap="word",
            borderwidth=1,
            relief="solid",
            font=("Segoe UI", 10),
        )
        self.text_box.grid(row=1, column=0, sticky="nsew", pady=(0, 10))
        self.text_box.configure(state="disabled")

        ttk.Label(conversation, text="Assistant answer").grid(
            row=2, column=0, sticky="w", pady=(0, 4)
        )
        self.answer_box = tk.Text(
            conversation,
            height=5,
            wrap="word",
            borderwidth=1,
            relief="solid",
            font=("Segoe UI", 10),
        )
        self.answer_box.grid(row=3, column=0, sticky="nsew")
        self.answer_box.configure(state="disabled")

        log_frame = ttk.Frame(body, padding=0)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(1, weight=1)

        ttk.Label(log_frame, text="Log").grid(row=0, column=0, sticky="w", pady=(0, 4))
        log_wrap = ttk.Frame(log_frame)
        log_wrap.grid(row=1, column=0, sticky="nsew")
        log_wrap.columnconfigure(0, weight=1)
        log_wrap.rowconfigure(0, weight=1)

        self.log_box = tk.Text(
            log_wrap,
            wrap="word",
            borderwidth=1,
            relief="solid",
            font=("Consolas", 9),
        )
        self.log_box.grid(row=0, column=0, sticky="nsew")
        self.log_box.configure(state="disabled")

        scrollbar = ttk.Scrollbar(log_wrap, orient="vertical", command=self.log_box.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log_box.configure(yscrollcommand=scrollbar.set)

        body.add(conversation, weight=2)
        body.add(log_frame, weight=3)

    def discover_wifi_esp(self) -> None:
        if self.started or self.discovering:
            return
        self.discovering = True
        self.discover_button.configure(state="disabled")
        self.thread_log("[wifi] discovering ESP32-C6 on UDP 3334...")
        threading.Thread(target=self._discover_worker, daemon=True).start()

    def _discover_worker(self) -> None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.bind(("", 3334))
            sock.settimeout(0.25)
        except Exception as exc:
            self.ui_queue.put(("discover_failed", f"[wifi] discovery failed: {exc}"))
            self.ui_queue.put(("discover_done", None))
            return

        try:
            deadline = time.time() + 6.0
            next_probe = 0.0
            while time.time() < deadline and not self.stop_event.is_set():
                now = time.time()
                if now >= next_probe:
                    probe = b"ESP32C6_MIC?"
                    for target in ("255.255.255.255", self.host_var.get().strip()):
                        if target:
                            try:
                                sock.sendto(probe, (target, 3334))
                            except OSError:
                                pass
                    next_probe = now + 0.8

                try:
                    data, addr = sock.recvfrom(512)
                except socket.timeout:
                    continue

                msg = data.decode("utf-8", errors="replace").strip()
                if not msg.startswith("ESP32C6_MIC;"):
                    continue

                fields = {}
                for part in msg.split(";")[1:]:
                    key, sep, value = part.partition("=")
                    if sep:
                        fields[key.strip()] = value.strip()

                host = fields.get("ip") or addr[0]
                try:
                    port = int(fields.get("tcp", "3333"))
                except ValueError:
                    port = 3333
                mode = fields.get("mode", "unknown")
                self.ui_queue.put(("wifi_discovered", (host, port, mode)))
                return

            self.ui_queue.put(("log", "[wifi] UDP not found, scanning local TCP :3333..."))
            found = self._scan_tcp_discovery()
            if found is not None:
                self.ui_queue.put(("wifi_discovered", (found, 3333, "tcp-scan")))
                return

            self.ui_queue.put(("discover_failed", "[wifi] ESP32-C6 not found"))
        finally:
            sock.close()
            self.ui_queue.put(("discover_done", None))

    def _local_ipv4_candidates(self) -> tuple[list[str], list[str]]:
        addresses = set()

        host_value = self.host_var.get().strip()
        if host_value:
            try:
                ipaddress.IPv4Address(host_value)
                addresses.add(host_value)
            except ValueError:
                pass

        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.connect(("8.8.8.8", 80))
                addresses.add(probe.getsockname()[0])
        except OSError:
            pass

        try:
            for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
                try:
                    ipaddress.IPv4Address(ip)
                except ValueError:
                    continue
                if not ip.startswith("127."):
                    addresses.add(ip)
        except OSError:
            pass

        hosts = []
        networks = []
        seen = set()
        for ip in sorted(addresses):
            network = ipaddress.ip_network(f"{ip}/24", strict=False)
            networks.append(str(network))
            for host in network.hosts():
                value = str(host)
                if value not in seen:
                    seen.add(value)
                    hosts.append(value)

            octets = ip.split(".")
            if len(octets) == 4:
                third = int(octets[2])
                for adjacent in (third - 1, third + 1):
                    if 0 <= adjacent <= 255:
                        adjacent_network = ipaddress.ip_network(
                            f"{octets[0]}.{octets[1]}.{adjacent}.0/24", strict=False
                        )
                        networks.append(str(adjacent_network))
                        for host in adjacent_network.hosts():
                            value = str(host)
                            if value not in seen:
                                seen.add(value)
                                hosts.append(value)
        return hosts, sorted(set(networks))

    def _tcp_probe_host(self, host: str) -> str | None:
        if self.stop_event.is_set():
            return None
        try:
            with socket.create_connection((host, 3333), timeout=0.25) as sock:
                sock.settimeout(0.35)
                deadline = time.time() + 1.2
                buf = bytearray()
                while time.time() < deadline and not self.stop_event.is_set():
                    try:
                        chunk = sock.recv(512)
                    except socket.timeout:
                        continue
                    if not chunk:
                        return None
                    buf.extend(chunk)
                    while len(buf) >= 2:
                        if buf[0] == assistant.MAGIC0 and buf[1] == assistant.MAGIC1:
                            return host
                        buf.pop(0)
                return None
        except OSError:
            return None

    def _scan_tcp_discovery(self) -> str | None:
        hosts, networks = self._local_ipv4_candidates()
        if not hosts:
            return None

        if networks:
            self.ui_queue.put(("log", f"[wifi] scanning networks: {', '.join(networks)}"))

        with concurrent.futures.ThreadPoolExecutor(max_workers=64) as executor:
            futures = {executor.submit(self._tcp_probe_host, host): host for host in hosts}
            for future in concurrent.futures.as_completed(futures):
                if self.stop_event.is_set():
                    return None
                try:
                    found = future.result()
                except OSError:
                    found = None
                if found:
                    return found
        return None

    def _queue_recording_state(self, active: bool, source: str = "") -> None:
        self.ui_queue.put(("recording", (active, source)))

    def _play_recording_beep(self) -> None:
        if winsound is None:
            try:
                self.root.bell()
            except Exception:
                pass
            return

        def worker() -> None:
            try:
                winsound.Beep(880, 130)
            except Exception:
                pass

        threading.Thread(target=worker, daemon=True).start()

    def _apply_recording_state(self, active: bool, source: str = "") -> None:
        if active:
            if not self.recording_active:
                self._play_recording_beep()
            self.recording_active = True
            suffix = f" ({source})" if source else ""
            self.recording_hint_var.set(f"ГОВОРИТЕ{suffix}")
            self.recording_hint.configure(bg="#2f6f4e", fg="white")
        else:
            self.recording_active = False
            self.recording_hint_var.set("Запись остановлена")
            self.recording_hint.configure(bg="#eceff3", fg="#5b6470")
            self.root.after(900, self._clear_recording_hint_if_idle)

    def _clear_recording_hint_if_idle(self) -> None:
        if not self.recording_active:
            self.recording_hint_var.set("")

    def start(self) -> None:
        if self.started:
            return
        self.started = True
        self.stop_event.clear()
        self.start_button.configure(state="disabled")
        self.transport_box.configure(state="disabled")
        self.port_entry.configure(state="disabled")
        self.host_entry.configure(state="disabled")
        self.tcp_port_entry.configure(state="disabled")
        self.discover_button.configure(state="disabled")
        self.pc_button.configure(state="normal")

        assistant.log = self.thread_log
        transport = self.transport_var.get()
        port = self.port_var.get().strip() or "COM5"
        host = self.host_var.get().strip() or "192.168.4.1"
        try:
            tcp_port = int(self.tcp_port_var.get().strip() or "3333")
        except ValueError:
            tcp_port = 3333
        self.thread_log("=" * 60)
        self.thread_log("Local voice assistant UI")
        self.thread_log(
            f"Transport: {transport} | ESP: {port if transport == 'Serial' else f'{host}:{tcp_port}'} "
            f"| ASR: {assistant.WHISPER_SIZE} | LLM: {assistant.OLLAMA_MODEL}"
        )
        self.thread_log("=" * 60)

        threading.Thread(target=self._brain_worker, daemon=True).start()
        if transport == "WiFi":
            threading.Thread(target=self._tcp_worker, args=(host, tcp_port), daemon=True).start()
        else:
            threading.Thread(target=self._serial_worker, args=(port,), daemon=True).start()

    def toggle_pc_recording(self) -> None:
        if sd is None:
            self.thread_log("[pc mic] sounddevice is not installed")
            return

        if not self.pc_recording:
            self.pc_chunks = []

            def callback(indata, frames, callback_time, status):
                if status:
                    self.thread_log(f"[pc mic] {status}")
                self.pc_chunks.append(indata[:, 0].copy())

            try:
                self.pc_stream = sd.InputStream(
                    samplerate=assistant.SAMPLE_RATE,
                    channels=1,
                    dtype="float32",
                    callback=callback,
                )
                self.pc_stream.start()
            except Exception as exc:
                self.pc_stream = None
                self.thread_log(f"[pc mic] open failed: {exc}")
                return

            self.pc_recording = True
            self.pc_button.configure(text="Stop PC mic")
            self._set_status("audio", "Audio: recording PC mic")
            self._queue_recording_state(True, "PC mic")
            self.thread_log("[pc mic] recording started")
        else:
            if self.pc_stream is not None:
                self.pc_stream.stop()
                self.pc_stream.close()
                self.pc_stream = None
            self.pc_recording = False
            self.pc_button.configure(text="PC mic")
            self._queue_recording_state(False)
            pcm = (
                np.concatenate(self.pc_chunks).astype(np.float32)
                if self.pc_chunks
                else np.zeros(0, dtype=np.float32)
            )
            self.pc_chunks = []
            self.submit_audio("pc mic", pcm)

    def _brain_worker(self) -> None:
        self._set_status("brain", "Brain: loading")
        try:
            brain = assistant.Brain()
        except Exception as exc:
            self.thread_log(f"[brain] init failed: {exc}")
            self._set_status("brain", "Brain: failed")
            return

        self._set_status("brain", "Brain: ready")
        while not self.stop_event.is_set():
            try:
                label, pcm = self.audio_queue.get(timeout=0.2)
            except queue.Empty:
                continue

            self._set_status("brain", "Brain: recognizing")
            text = brain.transcribe(pcm)
            self.ui_queue.put(("recognized", text))
            if not text:
                self.thread_log("[asr] empty result, skipped")
                self._set_status("brain", "Brain: ready")
                continue

            self._set_status("brain", "Brain: thinking")
            reply = brain.think(text)
            self.ui_queue.put(("answer", reply))

            self._set_status("brain", "Brain: speaking")
            brain.speak(reply)
            self._set_status("brain", "Brain: ready")

    def _serial_worker(self, port: str) -> None:
        try:
            ser = assistant.open_serial(port, assistant.BAUD_RATE, timeout=0.5)
        except Exception as exc:
            self.thread_log(f"[serial] open {port} failed: {exc}")
            self._set_status("serial", "Serial: failed")
            return

        self._set_status("serial", f"Serial: {port} connected")
        self.thread_log(f"[serial] connected to {port} @ {assistant.BAUD_RATE}")
        self.thread_log("[esp] C6: press D1 to start, press D1 again to stop.")

        buf = bytearray()
        current_pcm = bytearray()
        recording = False
        last_rx = time.time()
        warned_silence = False

        with ser:
            while not self.stop_event.is_set():
                try:
                    chunk = ser.read(4096)
                except Exception as exc:
                    self.thread_log(f"[serial] disconnected: {exc}")
                    self._set_status("serial", "Serial: disconnected")
                    return

                if chunk:
                    last_rx = time.time()
                    warned_silence = False
                    buf.extend(chunk)
                elif not warned_silence and time.time() - last_rx > 5:
                    warned_silence = True
                    self.thread_log("[serial] no data from ESP for 5 seconds")

                while len(buf) >= 4:
                    if buf[0] != assistant.MAGIC0 or buf[1] != assistant.MAGIC1:
                        buf.pop(0)
                        continue

                    frame_type = buf[2]
                    if frame_type == assistant.TYPE_EVENT:
                        code = buf[3]
                        event_len = 4 + assistant.EVENT_PAYLOAD_LENGTHS.get(code, 0)
                        if len(buf) < event_len:
                            break
                        del buf[:event_len]
                        if code == 1:
                            recording = True
                            current_pcm = bytearray()
                            self._set_status("audio", "Audio: recording ESP")
                            self._queue_recording_state(True, "ESP")
                            self.thread_log("[esp] recording started")
                        elif code == 0:
                            recording = False
                            self._queue_recording_state(False)
                            pcm_i16 = np.frombuffer(bytes(current_pcm), dtype=np.int16)
                            self.submit_audio("esp", pcm_i16.astype(np.float32) / 32768.0)
                            self._set_status("audio", "Audio: queued")

                    elif frame_type == assistant.TYPE_AUDIO:
                        if len(buf) < 5:
                            break
                        length = struct.unpack_from("<H", buf, 3)[0]
                        if len(buf) < 5 + length:
                            break
                        payload = bytes(buf[5 : 5 + length])
                        del buf[: 5 + length]
                        if recording:
                            current_pcm.extend(payload)
                    else:
                        buf.pop(0)

    def _tcp_worker(self, host: str, port: int) -> None:
        try:
            sock = socket.create_connection((host, port), timeout=10)
            sock.settimeout(0.5)
        except Exception as exc:
            self.thread_log(f"[wifi] connect {host}:{port} failed: {exc}")
            self._set_status("serial", "WiFi: failed")
            return

        self._set_status("serial", f"WiFi: {host}:{port} connected")
        self.thread_log(f"[wifi] connected to {host}:{port}")
        self.thread_log("[esp] C6: press D1 to start, press D1 again to stop.")

        buf = bytearray()
        current_pcm = bytearray()
        recording = False
        last_rx = time.time()
        warned_silence = False

        with sock:
            while not self.stop_event.is_set():
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        self.thread_log("[wifi] disconnected")
                        self._set_status("serial", "WiFi: disconnected")
                        return
                except socket.timeout:
                    chunk = b""
                except Exception as exc:
                    self.thread_log(f"[wifi] disconnected: {exc}")
                    self._set_status("serial", "WiFi: disconnected")
                    return

                if chunk:
                    last_rx = time.time()
                    warned_silence = False
                    buf.extend(chunk)
                elif not warned_silence and time.time() - last_rx > 5:
                    warned_silence = True
                    self.thread_log("[wifi] no data from ESP for 5 seconds")

                while len(buf) >= 4:
                    if buf[0] != assistant.MAGIC0 or buf[1] != assistant.MAGIC1:
                        buf.pop(0)
                        continue

                    frame_type = buf[2]
                    if frame_type == assistant.TYPE_EVENT:
                        code = buf[3]
                        event_len = 4 + assistant.EVENT_PAYLOAD_LENGTHS.get(code, 0)
                        if len(buf) < event_len:
                            break
                        del buf[:event_len]
                        if code == 1:
                            recording = True
                            current_pcm = bytearray()
                            self._set_status("audio", "Audio: recording ESP")
                            self._queue_recording_state(True, "ESP")
                            self.thread_log("[esp] recording started")
                        elif code == 0:
                            recording = False
                            self._queue_recording_state(False)
                            pcm_i16 = np.frombuffer(bytes(current_pcm), dtype=np.int16)
                            self.submit_audio("esp", pcm_i16.astype(np.float32) / 32768.0)
                            self._set_status("audio", "Audio: queued")

                    elif frame_type == assistant.TYPE_AUDIO:
                        if len(buf) < 5:
                            break
                        length = struct.unpack_from("<H", buf, 3)[0]
                        if len(buf) < 5 + length:
                            break
                        payload = bytes(buf[5 : 5 + length])
                        del buf[: 5 + length]
                        if recording:
                            current_pcm.extend(payload)
                    else:
                        buf.pop(0)

    def submit_audio(self, label: str, pcm: np.ndarray) -> None:
        seconds = len(pcm) / assistant.SAMPLE_RATE
        if len(pcm) < assistant.SAMPLE_RATE // 2:
            self.thread_log(f"[{label}] too short: {seconds:.1f}s")
            return

        pcm, gain, clipped_pct = assistant.condition_audio(pcm)
        rms = float(np.sqrt(np.mean(pcm.astype(np.float32) ** 2))) if len(pcm) else 0.0
        peak = float(np.max(np.abs(pcm))) if len(pcm) else 0.0
        self.thread_log(
            f"[{label}] audio: {seconds:.1f}s RMS={rms:.4f} peak={peak:.4f} "
            f"gain={gain:.1f} clipped={clipped_pct:.1f}%"
        )
        if clipped_pct > 1.0:
            self.thread_log(f"[{label}] warning: clipped input")

        try:
            import wave

            pcm16 = np.clip(pcm * 32768.0, -32768, 32767).astype(np.int16)
            with wave.open("last_record.wav", "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(assistant.SAMPLE_RATE)
                wav.writeframes(pcm16.tobytes())
            self.thread_log(f"[{label}] saved last_record.wav")
        except Exception as exc:
            self.thread_log(f"[{label}] wav save failed: {exc}")

        self.audio_queue.put((label, pcm.astype(np.float32)))

    def thread_log(self, message: str) -> None:
        self.ui_queue.put(("log", message))

    def _set_status(self, target: str, message: str) -> None:
        self.ui_queue.put((f"status:{target}", message))

    def _set_text(self, widget: tk.Text, text: str) -> None:
        widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("1.0", text)
        widget.configure(state="disabled")

    def _poll_ui_queue(self) -> None:
        while True:
            try:
                kind, value = self.ui_queue.get_nowait()
            except queue.Empty:
                break

            if kind == "log":
                self.log_box.configure(state="normal")
                self.log_box.insert("end", str(value) + "\n")
                self.log_box.see("end")
                self.log_box.configure(state="disabled")
            elif kind == "recognized":
                self._set_text(self.text_box, str(value))
            elif kind == "answer":
                self._set_text(self.answer_box, str(value))
            elif kind == "recording":
                active, source = value
                self._apply_recording_state(bool(active), str(source))
            elif kind == "wifi_discovered":
                host, port, mode = value
                self.transport_var.set("WiFi")
                self.host_var.set(str(host))
                self.tcp_port_var.set(str(port))
                self.log_box.configure(state="normal")
                self.log_box.insert(
                    "end", f"[wifi] found ESP32-C6 at {host}:{port} ({mode})\n"
                )
                self.log_box.see("end")
                self.log_box.configure(state="disabled")
            elif kind == "discover_failed":
                self.log_box.configure(state="normal")
                self.log_box.insert("end", str(value) + "\n")
                self.log_box.see("end")
                self.log_box.configure(state="disabled")
            elif kind == "discover_done":
                self.discovering = False
                if not self.started:
                    self.discover_button.configure(state="normal")
            elif kind == "status:serial":
                self.serial_status_var.set(str(value))
            elif kind == "status:brain":
                self.brain_status_var.set(str(value))
            elif kind == "status:audio":
                self.audio_status_var.set(str(value))

        self.root.after(80, self._poll_ui_queue)

    def clear_log(self) -> None:
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def _on_close(self) -> None:
        self.stop_event.set()
        if self.pc_stream is not None:
            try:
                self.pc_stream.stop()
                self.pc_stream.close()
            except Exception:
                pass
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    AssistantUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
