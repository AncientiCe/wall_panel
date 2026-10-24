"""Grab a screenshot from the panel over serial without resetting it.

Usage: python tools/screenshot.py COM5 out.png [command ...]
Optional commands (e.g. "page 2", "idle") are sent first, then "shot".
"""
import sys, time
import serial
from PIL import Image

port, out, cmds = sys.argv[1], sys.argv[2], sys.argv[3:]
p = serial.Serial()
p.port, p.baudrate, p.timeout = port, 115200, 2
p.dtr = p.rts = False  # opening with DTR/RTS asserted would reset the board
p.open()
for c in cmds:
    p.write((c + "\n").encode())
    time.sleep(1.5)
p.reset_input_buffer()
p.write(b"shot\n")

deadline = time.time() + 15
while True:
    line = p.readline()
    if line.startswith(b"SHOT"):
        break
    if time.time() > deadline:
        sys.exit("no SHOT header")
w, h = map(int, line.split()[1:3])
size = w * h * 2
data = bytearray()
while len(data) < size:
    chunk = p.read(size - len(data))
    if not chunk:
        sys.exit(f"timed out after {len(data)} of {size} bytes")
    data += chunk

img = Image.new("RGB", (w, h))
px = img.load()
for i in range(w * h):
    v = data[2 * i] | (data[2 * i + 1] << 8)
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    px[i % w, i // w] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
img.save(out)
print(out)
