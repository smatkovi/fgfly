#!/usr/bin/env python3
"""
acbake - aus FlightGears Flugzeugmodell (AC3D) ein Bündel für den kleinen
         Renderer machen.

Gelesen wird die `.ac`-Datei, wie sie im Hangar liegt: eine Hierarchie aus
Objekten mit Verschiebung, Drehung, Eckpunkten und Flächen. Herauskommt
dasselbe `.fgb`, das auch die Szenerie benutzt - nur mit Mittelpunkt (0,0,0),
woran der Renderer erkennt, dass es ein Modell ist und kein Stück Erde.

Was wegbleibt: alles, was von außen niemand sieht (Kanzelinnenraum,
Instrumentenbrett) und die unsichtbaren Schaltflächen der Bedienung
(`*HotSpot*`). Aus 6,7 MB werden damit ein paar hundert Kilobyte.

  acbake.py <modell.ac|modell.xml|<kennung>-set.xml> <ziel.fgb> [--innen]

Moderne Flugzeuge haben keine einzelne `.ac`-Datei mehr: das Aeussere wird aus
einem Baum von XML-Dateien zusammengesetzt - Rumpf, Fluegel, Leitwerke, Fahrwerk
und Triebwerke liegen je in einer eigenen `.ac`, und die XML sagt, wo sie
hingehoeren (`<offsets>` in Metern: x nach hinten, y nach rechts, z nach oben;
in der AC3D-Datei selbst heissen dieselben Achsen x, z, y).  Bekommt acbake eine
`-set.xml`, folgt es `sim/model/path` und baut alle Aussenteile in **ein**
Buendel.  Innenraum, Cockpit, Lichter und Bodengeraet bleiben weg.

Die Texturen werden gleich mit umgesetzt: je Textur eine `.tex` neben dem
Bündel, RGB565, wie bei der Szenerie.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from matcolors import png_average          # noqa: E402  (nur für den Rückfall)

SKIP_TEXTURES = re.compile(r"interior|panel|instrument", re.I)
SKIP_NAMES = re.compile(r"hotspot", re.I)
# Was am Flugzeug von aussen nicht zu sehen ist oder nicht dazugehoert.
SKIP_PART = re.compile(r"interior|flightdeck|cockpit|instrument|panel|/als/"
                       r"|(?<!f)light|strobe|beacon"
                       r"|fire|contrail|smoke|rain|shadow|pushback|baggage|cargo"
                       r"|groundservice|stair|crew|effects?/|/sounds?/", re.I)


def _text(node, tag):
    if node is None:
        return ""
    e = node.find(tag)
    return (e.text or "").strip() if e is not None and e.text else ""


def _offsets(node):
    """FlightGears Metermasse -> die Achsen der AC3D-Datei.

    In der XML heisst x nach hinten, y nach rechts, z nach oben; in der `.ac`
    sind es x nach hinten, y nach oben, z nach rechts.  Gedreht wird nichts -
    Winkel kommen im Aussenmodell praktisch nicht vor."""
    if node is None:
        return (0.0, 0.0, 0.0)
    def f(tag):
        t = _text(node, tag)
        try:
            return float(t)
        except ValueError:
            return 0.0
    return (f("x-m"), f("z-m"), f("y-m"))


def _resolve(raw, base_dir, pkg_dir):
    """`Aircraft/<paket>/Models/x.xml` zeigt vom Hangar aus, alles andere von
    der Datei aus, in der es steht."""
    raw = raw.strip().replace("\\", "/")
    if raw.startswith("Aircraft/"):
        parts = raw.split("/")
        return os.path.join(pkg_dir, *parts[2:])
    return os.path.join(base_dir, raw)


def model_parts(xml_path, pkg_dir, offset=(0.0, 0.0, 0.0), depth=0):
    """Die `.ac`-Teile eines XML-Modells, jedes mit seiner Lage im Flugzeug."""
    import xml.etree.ElementTree as ET
    out = []
    if depth > 6:
        return out
    try:
        root = ET.parse(xml_path).getroot()
    except Exception as exc:
        print("  %s nicht lesbar: %s" % (os.path.basename(xml_path), exc))
        return out
    base = os.path.dirname(os.path.abspath(xml_path))
    sim = root.find("sim")
    if sim is not None and sim.find("model") is not None:      # eine -set.xml
        path = _text(sim.find("model"), "path")
        if path:
            return model_parts(_resolve(path, base, pkg_dir), pkg_dir, offset, depth + 1)
        return out
    here = add3(offset, _offsets(root.find("offsets")))
    own = _text(root, "path")
    if own:
        p = _resolve(own, base, pkg_dir)
        # empty.ac ist der Platzhalter der Bemalungsverwaltung, keine Geometrie
        if p.endswith(".ac") and os.path.isfile(p) and os.path.getsize(p) > 400:
            out.append((p, here))
    for m in root.findall("model"):
        path = _text(m, "path")
        if not path:
            continue
        if SKIP_PART.search(_text(m, "name") + " " + path):
            continue
        if _text(m, "usage").lower() == "interior":
            continue
        p = _resolve(path, base, pkg_dir)
        off = add3(here, _offsets(m.find("offsets")))
        if p.endswith(".xml") and os.path.isfile(p):
            out += model_parts(p, pkg_dir, off, depth + 1)
        elif p.endswith(".ac") and os.path.isfile(p) and os.path.getsize(p) > 400:
            out.append((p, off))
    return out


def add3(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def package_dir(path):
    """Das Verzeichnis des Hangar-Pakets: das ueber `Models`, sonst das der
    Datei selbst."""
    d = os.path.dirname(os.path.abspath(path))
    while d != "/" and os.path.basename(d) in ("Models", "res") or \
            os.path.basename(os.path.dirname(d)) == "Models":
        d = os.path.dirname(d)
    return d


def shift_materials(obj, base):
    """Mehrere Dateien haben je eigene Materialnummern - die zweite Datei
    faengt hinter der ersten an."""
    obj.surfs = [(m + base, refs) for m, refs in obj.surfs]
    for kid in obj.kids:
        shift_materials(kid, base)


class Obj:
    def __init__(self):
        self.name = ""
        self.texture = ""
        self.loc = (0.0, 0.0, 0.0)
        self.rot = None
        self.verts = []
        self.surfs = []          # (mat, [(index, u, v), ...])
        self.kids = []


def parse(path):
    """AC3D lesen - nur die Zeilen, die Geometrie tragen."""
    lines = open(path, errors="replace").read().splitlines()
    pos = 0
    materials = []

    def read_object():
        nonlocal pos
        obj = Obj()
        pos += 1                                   # OBJECT <typ>
        while pos < len(lines):
            line = lines[pos].strip()
            word = line.split(" ", 1)[0]
            if word == "name":
                obj.name = line.split('"')[1] if '"' in line else ""
                pos += 1
            elif word == "texture":
                obj.texture = os.path.basename(line.split('"')[1]) if '"' in line else ""
                pos += 1
            elif word == "loc":
                obj.loc = tuple(float(v) for v in line.split()[1:4])
                pos += 1
            elif word == "rot":
                obj.rot = tuple(float(v) for v in line.split()[1:10])
                pos += 1
            elif word == "numvert":
                n = int(line.split()[1])
                pos += 1
                for _ in range(n):
                    obj.verts.append(tuple(float(v) for v in lines[pos].split()[:3]))
                    pos += 1
            elif word == "numsurf":
                n = int(line.split()[1])
                pos += 1
                for _ in range(n):
                    mat, refs = 0, []
                    while pos < len(lines):
                        s = lines[pos].strip()
                        if s.startswith("SURF"):
                            pos += 1
                        elif s.startswith("mat"):
                            mat = int(s.split()[1])
                            pos += 1
                        elif s.startswith("refs"):
                            count = int(s.split()[1])
                            pos += 1
                            for _k in range(count):
                                parts = lines[pos].split()
                                refs.append((int(parts[0]), float(parts[1]), float(parts[2])))
                                pos += 1
                            break
                        else:
                            pos += 1
                    obj.surfs.append((mat, refs))
            elif word == "kids":
                count = int(line.split()[1])
                pos += 1
                for _ in range(count):
                    while pos < len(lines) and not lines[pos].startswith("OBJECT"):
                        pos += 1
                    if pos < len(lines):
                        obj.kids.append(read_object())
                return obj
            else:
                pos += 1
        return obj

    while pos < len(lines):
        line = lines[pos]
        if line.startswith("MATERIAL"):
            m = re.search(r"rgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", line)
            materials.append(tuple(float(v) for v in m.groups()) if m else (0.8, 0.8, 0.8))
            pos += 1
        elif line.startswith("OBJECT"):
            return read_object(), materials
        else:
            pos += 1
    return Obj(), materials


MAX_EXTENT_M = 30.0        # groesser ist kein Flugzeugteil, sondern ein Effekt


dropped = []


def flatten(obj, xform, out, keep_inside):
    """Hierarchie auflösen: jedes Objekt mit seiner Weltlage in eine Liste."""
    loc = obj.loc
    rot = obj.rot
    base_off, base_rot = xform

    def apply(v):
        if rot:
            x = v[0] * rot[0] + v[1] * rot[3] + v[2] * rot[6]
            y = v[0] * rot[1] + v[1] * rot[4] + v[2] * rot[7]
            z = v[0] * rot[2] + v[1] * rot[5] + v[2] * rot[8]
            v = (x, y, z)
        v = (v[0] + loc[0], v[1] + loc[1], v[2] + loc[2])
        if base_rot:
            x = v[0] * base_rot[0] + v[1] * base_rot[3] + v[2] * base_rot[6]
            y = v[0] * base_rot[1] + v[1] * base_rot[4] + v[2] * base_rot[7]
            z = v[0] * base_rot[2] + v[1] * base_rot[5] + v[2] * base_rot[8]
            v = (x, y, z)
        return (v[0] + base_off[0], v[1] + base_off[1], v[2] + base_off[2])

    skip = SKIP_NAMES.search(obj.name or "")
    if not keep_inside and obj.texture and SKIP_TEXTURES.search(obj.texture):
        skip = True
    if obj.verts and obj.surfs and not skip:
        world = [apply(v) for v in obj.verts]
        # Die Lichtkegel von Lande- und Rollscheinwerfer sind 200 m lang und
        # gehoeren zur Beleuchtung, nicht zum Flugzeug.
        extent = max(max(abs(v[i]) for i in range(3)) for v in world)
        if extent <= MAX_EXTENT_M:
            out.append((obj, world))
        else:
            out.append(None)          # nur zum Zaehlen
            out.pop()
            dropped.append((obj.name, extent))

    child_off = apply((0.0, 0.0, 0.0))
    for kid in obj.kids:
        flatten(kid, (child_off, None), out, keep_inside)


def build(objects, materials):
    """Zu Dreiecken auflösen, nach Textur gruppieren, Normalen mitteln."""
    groups = {}
    for obj, verts in objects:
        key = obj.texture or "(ohne)"
        g = groups.setdefault(key, {"vertices": [], "index": [], "unique": {}})
        for mat, refs in obj.surfs:
            if len(refs) < 3:
                continue
            colour = materials[mat] if mat < len(materials) else (0.8, 0.8, 0.8)
            fan = [(refs[0], refs[i], refs[i + 1]) for i in range(1, len(refs) - 1)]
            for tri in fan:
                p = [verts[r[0]] for r in tri]
                ux, uy, uz = (p[1][i] - p[0][i] for i in range(3))
                vx, vy, vz = (p[2][i] - p[0][i] for i in range(3))
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                length = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
                normal = (nx / length, ny / length, nz / length)
                for corner, point in zip(tri, p):
                    # Aus dem Modell abgelesen (Propeller bei x=-1,8, Seitenruder
                    # bei x=+5,5, linke Flaeche bei z=+5,4, Bugrad bei y=-1,0):
                    # -x ist vorn, +y ist oben, -z ist rechts.
                    # Welt: x Ost (rechts), y Nord (vorn), z oben.
                    world = (-point[2], -point[0], point[1])
                    wnormal = (-normal[2], -normal[0], normal[1])
                    key2 = (round(world[0], 4), round(world[1], 4), round(world[2], 4),
                            round(corner[1], 4), round(corner[2], 4))
                    index = g["unique"].get(key2)
                    if index is None:
                        index = len(g["vertices"])
                        g["unique"][key2] = index
                        g["vertices"].append([world, list(wnormal), (corner[1], corner[2]),
                                              colour, 1])
                    else:
                        v = g["vertices"][index]
                        for i in range(3):
                            v[1][i] += wnormal[i]
                        v[4] += 1
                    g["index"].append(index)
    return groups


def write_bundle(path, groups):
    vertices, indices, ranges = [], [], []
    for name, g in groups.items():
        base = len(vertices)
        start = len(indices)
        for v in g["vertices"]:
            world, normal, uv, _colour, _n = v
            length = sum(c * c for c in normal) ** 0.5 or 1.0
            vertices.append((world, [c / length for c in normal], uv))
        indices.extend(base + i for i in g["index"])
        ranges.append((name, start, len(indices) - start))

    wide = len(vertices) > 65535
    names = b"".join(n.encode("latin-1") + b"\0" for n, _s, _c in ranges)
    with open(path, "wb") as f:
        f.write(b"FGB1")
        f.write(struct.pack("<I", 1 if wide else 0))
        f.write(struct.pack("<ddd", 0.0, 0.0, 0.0))      # Modell, kein Ort
        f.write(struct.pack("<f", 10.0))
        f.write(struct.pack("<III", len(vertices), len(indices), len(ranges)))
        f.write(struct.pack("<I", len(names)))
        f.write(names)
        for _n, start, count in ranges:
            f.write(struct.pack("<II", start, count))
        for world, normal, uv in vertices:
            f.write(struct.pack("<3f3b1x2f", world[0], world[1], world[2],
                                max(-127, min(127, int(round(normal[0] * 127)))),
                                max(-127, min(127, int(round(normal[1] * 127)))),
                                max(-127, min(127, int(round(normal[2] * 127)))),
                                uv[0], uv[1]))
        f.write(struct.pack("<%d%s" % (len(indices), "I" if wide else "H"), *indices))
    return len(vertices), len(indices), ranges


def find_texture(name, folders, pkg_dir, cache={}):
    """Die Bilddatei zu einem Texturnamen.  Erst dort, wo die `.ac` liegt,
    dann daneben - und wenn das nichts gibt, im ganzen Paket suchen: die
    Hangar-Pakete legen ihre Bilder gern zwei Ebenen daneben."""
    for folder in folders:
        for candidate in (os.path.join(folder, name),
                          os.path.join(folder, "..", name),
                          os.path.join(folder, "Exterior", name),
                          os.path.join(folder, "..", "Textures", name),
                          os.path.join(folder, "Textures", name)):
            if os.path.exists(candidate):
                return candidate
    if pkg_dir and pkg_dir not in cache:
        found = {}
        for root, _dirs, files in os.walk(pkg_dir):
            for f in files:
                found.setdefault(f, os.path.join(root, f))
        cache[pkg_dir] = found
    return cache.get(pkg_dir, {}).get(os.path.basename(name))


def write_textures(folders, pkg_dir, out_path, ranges):
    """Jede benutzte Textur als .tex daneben - RGB565, wie bei der Szenerie."""
    from matcolors import png_average       # nur um den Leser zu teilen
    stem = os.path.splitext(out_path)[0]
    made = 0
    for name, _s, _c in ranges:
        src = find_texture(name, folders, pkg_dir)
        if not src:
            print("    Bild fehlt: %s" % name)
            continue
        pixels = png_pixels(src)
        if not pixels:
            continue
        w, h, rgb = pixels
        # 2048er Texturen sind je 8 MB in RGB565; auf dem Geraet ist das zu
        # viel fuer ein Flugzeug, und 1024 sieht aus dieser Entfernung gleich aus.
        while w > 1024 or h > 1024:
            w2, h2 = w // 2, h // 2
            small = bytearray(w2 * h2 * 3)
            for y in range(h2):
                for x in range(w2):
                    o = (y * w2 + x) * 3
                    a = ((2 * y) * w + 2 * x) * 3
                    b = ((2 * y) * w + 2 * x + 1) * 3
                    c = ((2 * y + 1) * w + 2 * x) * 3
                    d = ((2 * y + 1) * w + 2 * x + 1) * 3
                    for k in range(3):
                        small[o + k] = (rgb[a + k] + rgb[b + k] + rgb[c + k] + rgb[d + k]) // 4
            rgb, w, h = small, w2, h2
        with open("%s.%s.tex" % (stem, os.path.splitext(name)[0]), "wb") as f:
            f.write(b"FGT1")
            f.write(struct.pack("<III", w, h, 0))
            f.write(struct.pack("<fff", 0.0, 0.0, 1.0))
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    i = (y * w + x) * 3
                    r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
                    row += struct.pack("<H", ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
                f.write(bytes(row))
        made += 1
    return made


def png_pixels(path):
    """Vollständiges PNG lesen (Farbtyp 2 und 6, acht Bit) - wie in matcolors,
    nur dass hier die Bildpunkte gebraucht werden und nicht ihr Mittel."""
    import zlib
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
            width, height, depth, colour, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if depth != 8 or colour not in (2, 6) or interlace:
        return None
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * 3)
    prev = bytearray(stride)
    at = 0
    for y in range(height):
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
        for x in range(width):
            i, o = x * channels, (y * width + x) * 3
            out[o:o + 3] = line[i:i + 3]
    return width, height, out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip())
    ac_path, out_path = sys.argv[1], sys.argv[2]
    keep_inside = "--innen" in sys.argv

    objects, materials, folders = [], [], []
    pkg = package_dir(ac_path)
    if ac_path.endswith(".xml"):
        parts = model_parts(ac_path, pkg)
        if not parts:
            sys.exit("keine .ac-Teile in %s" % ac_path)
        print("%s: %d Teile" % (os.path.basename(ac_path), len(parts)))
        for path, off in parts:
            root, mats = parse(path)
            shift_materials(root, len(materials))
            materials += mats
            before = len(objects)
            flatten(root, (off, None), objects, keep_inside)
            print("    %-28s x%+7.2f y%+6.2f z%+6.2f  %3d Objekte" %
                  (os.path.basename(path), off[0], off[1], off[2],
                   len(objects) - before))
            folders.append(os.path.dirname(os.path.abspath(path)))
    else:
        root, materials = parse(ac_path)
        flatten(root, ((0.0, 0.0, 0.0), None), objects, keep_inside)
        folders = [os.path.dirname(os.path.abspath(ac_path))]

    groups = build(objects, materials)
    nv, ni, ranges = write_bundle(out_path, groups)
    made = write_textures(folders, pkg, out_path, ranges)

    print("%s -> %s" % (os.path.basename(ac_path), out_path))
    print("  %d Objekte, %d Eckpunkte, %d Dreiecke, %d Texturgruppen, %d Bilder" %
          (len(objects), nv, ni // 3, len(ranges), made))
    for name, extent in dropped:
        print("    weggelassen: %-24s %.0f m gross" % (name, extent))
    print("  %.0f KB Bündel" % (os.path.getsize(out_path) / 1024.0))
    for name, _s, count in ranges:
        print("    %-24s %6d Dreiecke" % (name, count // 3))


if __name__ == "__main__":
    main()
