#!/usr/bin/env python3
"""
btgbake - liest FlightGears Szenerie-Kacheln (.btg.gz) und backt sie zu dem,
was ein kleiner OpenGL-ES-2-Renderer auf dem Nokia N9 zeichnen koennte.

FlightGear haelt eine Kachel als getrennte Listen fuer Eckpunkte, Normalen und
Texturkoordinaten, dazu je Material eine Liste von Dreiecken, deren drei Indizes
in verschiedene Listen zeigen. GL kennt nur einen Index je Eckpunkt. Backen
heisst hier: die Tripel eindeutig machen, einen einzigen verschachtelten
Eckpunktpuffer bauen und je Material einen zusammenhaengenden Indexbereich -
aus tausenden Zeichenaufrufen wird einer je Material.

  btgbake.py census <verzeichnis>      zaehlt ueber alle Kacheln darunter
  btgbake.py bake <kachel.btg.gz> [ziel.fgb]

Das Format der Kachel ist SimGears SGBinObject (sg_binobj.cxx): little endian,
Kopf 'SG' plus Version, dann Objekte aus Eigenschaften und Elementen. Version 7
zaehlt mit 16 Bit und indiziert mit 16 Bit, ab Version 10 mit 32.
"""
import glob
import gzip
import os
import struct
import sys
from collections import OrderedDict

# Objekttypen aus sg_binobj.hxx
BOUNDING_SPHERE, VERTEX_LIST, NORMAL_LIST, TEXCOORD_LIST, COLOR_LIST = 0, 1, 2, 3, 4
POINTS, TRIANGLE_FACES, TRIANGLE_STRIPS, TRIANGLE_FANS = 9, 10, 11, 12
# Eigenschaften
PROP_MATERIAL, PROP_INDEX_TYPES = 0, 1
# Bits in PROP_INDEX_TYPES
IDX_VERTICES, IDX_NORMALS, IDX_COLORS, IDX_TEXCOORDS = 1, 2, 4, 8


class Tile:
    """Eine gelesene Kachel: rohe Listen und die Dreiecksgruppen je Material."""

    def __init__(self, path, index_size=None):
        self.path = path
        self.forced_index_size = index_size
        self.center = (0.0, 0.0, 0.0)
        self.radius = 0.0
        self.vertices = []      # (x, y, z) relativ zum Mittelpunkt
        self.normals = []       # (nx, ny, nz), aus je drei Bytes
        self.texcoords = []     # (u, v)
        self.groups = OrderedDict()   # Material -> Liste von (vi, ni, ti) Tripeln
        self._read()
        # Ab Version 10 koennen die Indizes 16 oder 32 Bit breit sein. Welche es
        # sind, sagt die Datei nicht - aber ein Index zeigt immer in die
        # Eckpunktliste, und mit der falschen Breite tut er das nicht.
        if self.forced_index_size is None and self._indices_out_of_range():
            self.__init__(path, index_size=2 if self.index_size == 4 else 4)

    def _read(self):
        data = gzip.open(self.path, "rb").read()
        pos = 0

        def take(fmt):
            nonlocal pos
            out = struct.unpack_from(fmt, data, pos)
            pos += struct.calcsize(fmt)
            return out

        header, _ctime = take("<II")
        if (header >> 16) != 0x5347:
            raise ValueError("%s ist keine .btg-Datei" % self.path)
        version = header & 0xFFFF
        self.version = version
        wide = version >= 10
        count_fmt = "<I" if wide else "<H"
        index_size = self.forced_index_size or (4 if wide else 2)
        index_fmt = "I" if index_size == 4 else "H"
        self.index_size = index_size

        (nobjects,) = take(count_fmt)
        for _ in range(nobjects):
            (obj_type,) = take("<b")
            nprops, nelements = take("<II" if wide else "<HH")
            material, index_types = None, IDX_VERTICES
            for _ in range(nprops):
                (prop_type,) = take("<b")
                (nbytes,) = take("<I")
                raw = data[pos:pos + nbytes]
                pos += nbytes
                if prop_type == PROP_MATERIAL:
                    material = raw.decode("latin-1").strip("\0")
                elif prop_type == PROP_INDEX_TYPES and raw:
                    index_types = raw[0]

            for _ in range(nelements):
                (nbytes,) = take("<I")
                raw = data[pos:pos + nbytes]
                pos += nbytes
                if obj_type == BOUNDING_SPHERE:
                    x, y, z, r = struct.unpack("<dddf", raw)
                    self.center, self.radius = (x, y, z), r
                elif obj_type == VERTEX_LIST:
                    self.vertices = list(struct.iter_unpack("<fff", raw))
                elif obj_type == NORMAL_LIST:
                    # drei Bytes je Normale, 0..255 auf -1..1 abgebildet
                    # drei Bytes, geschrieben als (n + 1) * 127.5 und dabei
                    # abgeschnitten: das halbe Byte wieder aufschlagen, sonst
                    # sind die Normalen im Mittel 1 % zu kurz.
                    self.normals = [((a + 0.5) / 127.5 - 1.0, (b + 0.5) / 127.5 - 1.0,
                                     (c + 0.5) / 127.5 - 1.0)
                                    for a, b, c in struct.iter_unpack("<BBB", raw)]
                elif obj_type == TEXCOORD_LIST:
                    self.texcoords = list(struct.iter_unpack("<ff", raw))
                elif obj_type in (TRIANGLE_FACES, TRIANGLE_STRIPS, TRIANGLE_FANS):
                    tris = self._element_triangles(raw, obj_type, index_types,
                                                   index_fmt, index_size)
                    self.groups.setdefault(material or "", []).extend(tris)
        self.bytes_read = pos
        self.bytes_total = len(data)

    @staticmethod
    def _element_triangles(raw, obj_type, index_types, index_fmt, index_size):
        """Ein Element zu Dreiecken aufloesen, als Tripel (vertex, normal, texcoord)."""
        per_vertex = (bool(index_types & IDX_VERTICES) + bool(index_types & IDX_NORMALS) +
                      bool(index_types & IDX_COLORS) + bool(index_types & IDX_TEXCOORDS))
        count = len(raw) // (index_size * per_vertex)
        values = struct.unpack("<%d%s" % (count * per_vertex, index_fmt), raw)

        corners = []
        for i in range(count):
            slot = list(values[i * per_vertex:(i + 1) * per_vertex])
            vi = slot.pop(0) if index_types & IDX_VERTICES else 0
            ni = slot.pop(0) if index_types & IDX_NORMALS else -1
            if index_types & IDX_COLORS:
                slot.pop(0)
            ti = slot.pop(0) if index_types & IDX_TEXCOORDS else -1
            corners.append((vi, ni, ti))

        if obj_type == TRIANGLE_FACES:
            return [corners[i:i + 3] for i in range(0, len(corners) - 2, 3)]
        out = []
        if obj_type == TRIANGLE_FANS:
            for i in range(1, len(corners) - 1):
                out.append([corners[0], corners[i], corners[i + 1]])
        else:   # Streifen, jedes zweite Dreieck umgekehrt
            for i in range(len(corners) - 2):
                out.append([corners[i], corners[i + 1], corners[i + 2]] if i % 2 == 0
                           else [corners[i + 1], corners[i], corners[i + 2]])
        return out

    def _indices_out_of_range(self):
        limit = len(self.vertices)
        for tris in self.groups.values():
            for tri in tris:
                for vi, _ni, _ti in tri:
                    if vi >= limit:
                        return True
        return False

    @property
    def triangles(self):
        return sum(len(t) for t in self.groups.values())


def bake(tile):
    """Tripel eindeutig machen: ein Eckpunktpuffer, je Material ein Indexbereich."""
    unique = {}
    vertices = []
    ranges = OrderedDict()
    indices = []
    for material, tris in tile.groups.items():
        start = len(indices)
        for tri in tris:
            for corner in tri:
                index = unique.get(corner)
                if index is None:
                    index = len(vertices)
                    unique[corner] = index
                    vi, ni, ti = corner
                    vertices.append((tile.vertices[vi] if vi < len(tile.vertices) else (0, 0, 0),
                                     tile.normals[ni] if 0 <= ni < len(tile.normals) else (0, 1, 0),
                                     tile.texcoords[ti] if 0 <= ti < len(tile.texcoords) else (0, 0)))
                indices.append(index)
        ranges[material] = (start, len(indices) - start)
    return vertices, indices, ranges


def write_bundle(path, tile, vertices, indices, ranges):
    """Ein Buendel, das ein ES-2-Renderer ohne Umrechnen hochladen kann."""
    wide = len(vertices) > 65535
    names = b"".join(m.encode("latin-1") + b"\0" for m in ranges)
    with open(path, "wb") as out:
        out.write(b"FGB1")
        out.write(struct.pack("<I", 1 if wide else 0))
        out.write(struct.pack("<ddd", *tile.center))
        out.write(struct.pack("<f", tile.radius))
        out.write(struct.pack("<III", len(vertices), len(indices), len(ranges)))
        out.write(struct.pack("<I", len(names)))
        out.write(names)
        for start, count in ranges.values():
            out.write(struct.pack("<II", start, count))
        for (x, y, z), (nx, ny, nz), (u, v) in vertices:
            out.write(struct.pack("<3f3b1x2f", x, y, z,
                                  max(-127, min(127, int(round(nx * 127)))),
                                  max(-127, min(127, int(round(ny * 127)))),
                                  max(-127, min(127, int(round(nz * 127)))), u, v))
        out.write(struct.pack("<%d%s" % (len(indices), "I" if wide else "H"), *indices))
    return os.path.getsize(path)


def census(root):
    files = sorted(glob.glob(os.path.join(root, "**", "*.btg.gz"), recursive=True))
    if not files:
        sys.exit("keine .btg.gz unter %s" % root)
    total_tris = total_verts = 0
    materials = {}
    print("%-14s %3s %9s %9s %7s  %s" % ("Kachel", "Ver", "Eckpunkte", "Dreiecke",
                                        "Mat.", "groesstes Material"))
    for path in files:
        tile = Tile(path)
        biggest = max(tile.groups.items(), key=lambda kv: len(kv[1]))[0] if tile.groups else "-"
        print("%-14s %3d %9d %9d %7d  %s" % (os.path.basename(path), tile.version,
                                             len(tile.vertices), tile.triangles,
                                             len(tile.groups), biggest))
        total_tris += tile.triangles
        total_verts += len(tile.vertices)
        for material, tris in tile.groups.items():
            materials[material] = materials.get(material, 0) + len(tris)
    print("\n%d Kacheln, %d Eckpunkte, %d Dreiecke, %d Materialien" %
          (len(files), total_verts, total_tris, len(materials)))
    print("Dreiecke je Material, die zehn groessten:")
    for material, count in sorted(materials.items(), key=lambda kv: -kv[1])[:10]:
        print("  %-24s %8d  %4.1f %%" % (material, count, 100.0 * count / total_tris))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip())
    mode, target = sys.argv[1], sys.argv[2]
    if mode == "census":
        census(target)
        return
    if mode != "bake":
        sys.exit(__doc__.strip())

    tile = Tile(target)
    vertices, indices, ranges = bake(tile)
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(
        os.path.splitext(target)[0])[0] + ".fgb"
    size = write_bundle(out, tile, vertices, indices, ranges)
    print("%s, Version %d" % (os.path.basename(target), tile.version))
    print("  gelesen        %d Eckpunkte, %d Normalen, %d Texturkoordinaten" %
          (len(tile.vertices), len(tile.normals), len(tile.texcoords)))
    print("  Gruppen        %d Materialien, %d Dreiecke" % (len(tile.groups), tile.triangles))
    print("  gebacken       %d Eckpunkte (%.2fx), %d Indizes, %d Zeichenaufrufe" %
          (len(vertices), len(vertices) / max(1, len(tile.vertices)), len(indices), len(ranges)))
    print("  %s  %.1f MB  (%d Bytes je Eckpunkt)" % (out, size / 1048576.0, 24))


if __name__ == "__main__":
    main()
