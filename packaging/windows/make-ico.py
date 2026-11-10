#!/usr/bin/env python3
"""Arma packaging/windows/moss.ico con el ícono de Linux (el cuadro redondeado).

Se corre a mano cuando cambia el SVG (necesita rsvg-convert) y el .ico se
guarda en el repositorio: la CI de Windows no tiene rsvg-convert.

  python3 -I packaging/windows/make-ico.py
"""

import pathlib
import struct
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
svg = root / "packaging/linux/io.github.ricky182771.moss.svg"
sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
# Un PNG por tamaño (el formato de ICO lo acepta desde Windows Vista).
pngs = [subprocess.run(["rsvg-convert", "-w", str(s), "-h", str(s), str(svg)],
                       check=True, capture_output=True).stdout for s in sizes]
offset = 6 + 16 * len(sizes)
out = struct.pack("<HHH", 0, 1, len(sizes))
for size, png in zip(sizes, pngs):
    out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
(root / "packaging/windows/moss.ico").write_bytes(out + b"".join(pngs))
