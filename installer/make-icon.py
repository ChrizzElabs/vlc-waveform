"""Reproducible original Waveform icon; Pillow is build-time only.
No downloaded artwork, fonts, VLC logo or runtime image dependency.
"""
from pathlib import Path
from PIL import Image, ImageDraw

size = 512
image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((18, 18, 494, 494), radius=112, fill='#111c28', outline='#365165', width=8)
draw.rounded_rectangle((40, 40, 472, 472), radius=92, outline='#233e50', width=3)
heights = [30, 62, 108, 165, 126, 78, 44]
for x, height in zip(range(106, 407, 50), heights):
    draw.rounded_rectangle((x-12, 266-height, x+12, 266+height), radius=12,
                           fill='#89eee2' if x < 256 else '#46becb')
draw.line((256, 100, 256, 430), fill='#ffb04e', width=9)
draw.ellipse((244, 88, 268, 112), fill='#ffb04e')
image.save(Path(__file__).with_name('waveform.ico'), sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
