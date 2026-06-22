#!/usr/bin/env python3
import argparse
import datetime as dt
import struct
import time

import serial

MAGIC = b"\xA5\x5A"
TYPE_AUDIO = 0x01
TYPE_EVENT = 0x02


def open_serial(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def ts():
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def printable(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--out", default=r"D:\test\esp_serial_log.txt")
    args = parser.parse_args()

    end = time.time() + args.seconds
    buf = bytearray()
    raw_bytes = 0
    events = 0
    audio_frames = 0

    with open(args.out, "w", encoding="utf-8") as log:
        def write(line: str):
            print(line)
            log.write(line + "\n")
            log.flush()

        write(f"{ts()} START port={args.port} baud={args.baud} seconds={args.seconds}")

        try:
            ser = open_serial(args.port, args.baud, timeout=0.2)
        except Exception as e:
            write(f"{ts()} ERROR open serial: {e}")
            return

        with ser:
            while time.time() < end:
                data = ser.read(4096)
                if not data:
                    continue

                raw_bytes += len(data)
                write(f"{ts()} RAW len={len(data)} hex={data[:64].hex(' ')} ascii={printable(data[:64])}")
                buf.extend(data)

                while len(buf) >= 4:
                    if buf[:2] != MAGIC:
                        del buf[0]
                        continue

                    frame_type = buf[2]
                    if frame_type == TYPE_EVENT:
                        code = buf[3]
                        value = None
                        value2 = None
                        if code in (12, 13):
                            if len(buf) < 6:
                                break
                            value = buf[4]
                            value2 = buf[5]
                            del buf[:6]
                        elif code in (9, 11) and len(buf) >= 5:
                            value = buf[4]
                            del buf[:5]
                        else:
                            del buf[:4]
                        events += 1
                        if code == 1:
                            write(f"{ts()} EVENT start button_down")
                        elif code == 0:
                            write(f"{ts()} EVENT stop button_up")
                        elif code == 9:
                            write(f"{ts()} EVENT heartbeat button_level={value}")
                        elif code == 10:
                            write(f"{ts()} EVENT i2s_ok")
                        elif code == 11:
                            write(f"{ts()} EVENT i2s_fail err_low_byte={value}")
                        elif code == 12:
                            write(f"{ts()} EVENT gpio_change pin={value} level={value2}")
                        elif code == 13:
                            write(f"{ts()} EVENT gpio_state pin={value} level={value2}")
                        else:
                            write(f"{ts()} EVENT unknown code={code}")
                    elif frame_type == TYPE_AUDIO:
                        if len(buf) < 5:
                            break
                        length = struct.unpack_from("<H", buf, 3)[0]
                        if len(buf) < 5 + length:
                            break
                        del buf[:5 + length]
                        audio_frames += 1
                        write(f"{ts()} AUDIO len={length}")
                    else:
                        write(f"{ts()} BAD_FRAME type=0x{frame_type:02x}")
                        del buf[0]

        write(
            f"{ts()} DONE raw_bytes={raw_bytes} events={events} "
            f"audio_frames={audio_frames} leftover={len(buf)}"
        )


if __name__ == "__main__":
    main()
