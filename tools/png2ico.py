# Convert a PNG to a minimal ICO (Windows Vista+ PNG-in-ICO format).
# Usage: python png2ico.py input.png output.ico
import struct, sys

with open(sys.argv[1], 'rb') as f:
    png = f.read()

# ICO header: reserved=0, type=1 (ICO), count=1
header = struct.pack('<HHH', 0, 1, 1)
# Directory entry: w=0(256), h=0, colors=0, reserved=0, planes=1, bpp=32, size, offset=22
entry = struct.pack('<BBBBHHII', 0, 0, 0, 0, 1, 32, len(png), 22)

with open(sys.argv[2], 'wb') as f:
    f.write(header + entry + png)
