"""Converts a --ui-preview BMP screenshot to PNG.

    python -I tools/bmp_to_png.py in.bmp out.png
"""
import sys
from PIL import Image

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: bmp_to_png.py in.bmp out.png")
    Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2], optimize=True)

if __name__ == "__main__":
    main()
