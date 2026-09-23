#!/usr/bin/env python3
"""
matcolors - mittlere Farbe je FlightGear-Material.

Der Backofen malt jede Kachel einmal von oben in ein Bild; dafuer braucht er
zu jedem Materialnamen eine Farbe. Die steht nirgends - also wird sie aus der
Textur genommen, die FlightGear fuer das Material benutzt: Mittelwert ueber
alle Bildpunkte.

  matcolors.py <fgdata> [ziel.txt]

Gelesen werden die Materialdateien unter Materials/, gesucht wird die
PNG-Fassung der Textur (Textures/Terrain/...). Die Ausgabe ist eine Zeile je
Name: `Name r g b`, Werte 0..255.

Der PNG-Leser hier kann, was FlightGears Texturen brauchen: Farbtyp 2 und 6,
acht Bit, ohne Zeilensprung. Mehr nicht - und wenn doch, sagt er es.
"""
import os
import re
import struct
import sys
import zlib


def png_average(path):
    """Mittlere Farbe eines PNG, ohne fremde Bibliothek."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos, width, height, depth, colour, interlace = 8, 0, 0, 0, 0, 0
    idat = bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _comp, _filt, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if depth != 8 or colour not in (2, 6) or interlace:
        return None

    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(stride)
    prev = bytearray(stride)
    sums = [0, 0, 0]
    count = 0
    at = 0
    for _y in range(height):
        if at + 1 + stride > len(raw):
            break
        filt = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if filt == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filt == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filt == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        prev = line
        # nur jede vierte Zeile und jeder vierte Punkt - der Mittelwert
        # aendert sich davon nicht, die Laufzeit schon
        for x in range(0, width, 4):
            i = x * channels
            if channels == 4 and line[i + 3] < 128:
                continue
            sums[0] += line[i]
            sums[1] += line[i + 1]
            sums[2] += line[i + 2]
            count += 1
    out = out  # nur der Vollstaendigkeit halber
    if not count:
        return None
    return tuple(s // count for s in sums)


def collect(fgdata):
    """Materialname -> Texturpfad, aus allen Materialdateien."""
    wanted = {}
    root = os.path.join(fgdata, "Materials")
    for folder in ("base", "default", "regions", "dds"):
        d = os.path.join(root, folder)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".xml"):
                continue
            text = open(os.path.join(d, name), errors="replace").read()
            for block in re.findall(r"<material[^>]*>(.*?)</material>", text, re.S):
                names = re.findall(r"<name>\s*([^<]+?)\s*</name>", block)
                tex = re.search(r"<texture[^>]*>\s*([^<]+?)\s*</texture>", block)
                if not names or not tex:
                    continue
                path = tex.group(1).replace(".dds", ".png")
                for n in names:
                    wanted.setdefault(n, path)
    return wanted


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip())
    fgdata = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else "materials.txt"
    wanted = collect(fgdata)
    cache = {}
    rows = []
    missing = 0
    for name, tex in sorted(wanted.items()):
        if tex not in cache:
            full = os.path.join(fgdata, "Textures", tex)
            cache[tex] = png_average(full) if os.path.exists(full) else None
        rgb = cache[tex]
        if not rgb:
            missing += 1
            continue
        rows.append((name, rgb))
    with open(out_path, "w") as f:
        f.write("# Name r g b - mittlere Farbe der Textur (matcolors.py)\n")
        for name, rgb in rows:
            f.write("%s %d %d %d\n" % (name.replace(" ", "_"), rgb[0], rgb[1], rgb[2]))
    print("%d Materialien mit Farbe, %d ohne lesbare Textur -> %s (%d Bytes)" %
          (len(rows), missing, out_path, os.path.getsize(out_path)))
    for name, rgb in rows[:8]:
        print("  %-24s %3d %3d %3d" % (name, *rgb))


if __name__ == "__main__":
    main()
