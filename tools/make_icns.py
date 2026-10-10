#!/usr/bin/env python3
"""Builds a macOS .icns from a PNG (PNG-encoded entries, macOS 10.7+):
make_icns.py <icon.png> <out.icns>. Sizes above the source are not upscaled."""
import io, struct, sys
from PIL import Image

src = Image.open(sys.argv[1]).convert("RGBA")
# (type, size) pairs, PNG-encoded: icp4/icp5/icp6 = 16/32/64, ic07/ic08/ic09/ic10 = 128/256/512/1024,
# and the Retina "@2x" entries ic11/ic12/ic13/ic14 = 16/32/128/256 at double size
entries = [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128), ("ic08", 256), ("ic09", 512), ("ic10", 1024),
           ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)]
chunks = b""
for kind, size in entries:
	if size > max(src.size):
		continue
	img = src.resize((size, size), Image.LANCZOS) if size != src.size[0] else src
	buf = io.BytesIO()
	img.save(buf, "PNG")
	data = buf.getvalue()
	chunks += kind.encode() + struct.pack(">I", 8 + len(data)) + data
with open(sys.argv[2], "wb") as f:
	f.write(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)
print("wrote", sys.argv[2], len(chunks) + 8, "bytes")
