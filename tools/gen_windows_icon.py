#!/usr/bin/env python3
"""Writes cmake/windows/hailstone.ico, the icon built into hailstone.exe (cmake/windows/hailstone.rc), from
icons/hailstone.png: one image for each size Windows asks for (title bar, taskbar, Explorer, Alt+Tab at each scale).

usage: tools/gen_windows_icon.py icons/hailstone.png cmake/windows/hailstone.ico   (needs Pillow)
"""
import sys

from PIL import Image

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

src = Image.open(sys.argv[1]).convert("RGBA")
src.save(sys.argv[2], format="ICO", sizes=[(s, s) for s in SIZES])
