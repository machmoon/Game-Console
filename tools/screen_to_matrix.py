#!/usr/bin/env python3
"""
Capture the computer screen, downsample to 32x8, threshold to on/off,
and stream frames to an Arduino MAX7219 receiver.

Protocol sent each frame:
  F,<64 hex chars>\n
Where 64 hex chars = 8 rows * 4 bytes per row (32 bits wide).
"""

import argparse
import time

from PIL import ImageGrab, ImageOps
import serial


def frame_to_hex(img_32x8):
    # img is mode 'L' grayscale, 32x8
    pixels = img_32x8.load()
    row_bytes = []

    for y in range(8):
        for block in range(4):
            b = 0
            for col in range(8):
                x = block * 8 + col
                on = pixels[x, y] > 127
                if on:
                    b |= 1 << (7 - col)
            row_bytes.append(b)

    return "".join(f"{b:02X}" for b in row_bytes)


def main():
    parser = argparse.ArgumentParser(description="Mirror PC screen to MAX7219 8x32 via serial")
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbmodem1101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--fps", type=float, default=15.0)
    parser.add_argument("--invert", action="store_true", help="Invert black/white mapping")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0)
    print(f"Streaming to {args.port} @ {args.baud}")

    frame_dt = 1.0 / max(1.0, args.fps)
    next_t = time.time()

    try:
        while True:
            shot = ImageGrab.grab(all_screens=True)
            gray = ImageOps.grayscale(shot).resize((32, 8))
            if args.invert:
                gray = ImageOps.invert(gray)

            payload = frame_to_hex(gray)
            line = f"F,{payload}\n"
            ser.write(line.encode("ascii"))

            next_t += frame_dt
            sleep_t = next_t - time.time()
            if sleep_t > 0:
                time.sleep(sleep_t)
            else:
                next_t = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
