#!/usr/bin/env python3
"""
es2ify - build an OpenGL ES 2.0 test corpus out of an OSG_GLES_DUMP_SHADERS run.

The dump writes one file per shader, each holding the converted GLSL ES 3.00
text that the device got and, below it, the original the engine handed in.  The
files are named by hash, so the vertex/fragment pairing is recovered from the
run's log: OSG dumps a program's shaders one after the other and then logs its
attribute bindings.

What comes out is a directory of <program>.vert/.frag in GLSL ES 1.00 - what an
OpenGL ES 2.0 device such as the PowerVR SGX530 in the Nokia N9 would have to
compile - plus a report of what each program demands, so that a driver's
"too many varyings" can be predicted before asking it.

  es2ify.py <dump-dir> <fgfs-log> <out-dir>
"""
import os
import re
import sys
from collections import OrderedDict

CONVERTED = "/* ---- converted ---- */"
ORIGINAL = "/* ---- original ---- */"

# Constructs GLSL ES 1.00 does not have.  Reported, not rewritten: a hit means
# the shader needs real work, not a search and replace.
ES2_GAPS = [
    (r"\bflat\b", "flat interpolation"),
    (r"\blayout\s*\(", "layout qualifier"),
    (r"\bgl_VertexID\b", "gl_VertexID"),
    (r"\bgl_InstanceID\b", "gl_InstanceID"),
    (r"\btextureLod\s*\(|\btexture2DLodEXT\b|\btextureCubeLodEXT\b", "explicit LOD (needs EXT_shader_texture_lod)"),
    (r"\bgl_FragDepth\b", "fragment depth (needs EXT_frag_depth)"),
    (r"\btextureGrad\s*\(", "textureGrad"),
    (r"\btexelFetch\s*\(", "texelFetch"),
    (r"\bdFdx\s*\(|\bdFdy\s*\(|\bfwidth\s*\(", "derivatives (need OES_standard_derivatives)"),
    (r"\bsampler3D\b", "sampler3D (needs OES_texture_3D)"),
    (r"\bisampler|\busampler|\bsampler2DArray\b", "integer or array sampler"),
    (r"<<|>>|[^&]&[^&]|[^|]\|[^|]", "bit operator"),
    (r"\buint\b|\buvec[234]\b", "unsigned integer type"),
    (r"\bswitch\s*\(", "switch"),
]

VEC_SIZE = {"float": 1, "vec2": 1, "vec3": 1, "vec4": 1,
            "mat2": 2, "mat3": 3, "mat4": 4,
            "int": 1, "ivec2": 1, "ivec3": 1, "ivec4": 1}


def split_dump(text):
    """Return the converted section of a dump file."""
    if CONVERTED not in text:
        return text
    body = text.split(CONVERTED, 1)[1]
    return body.split(ORIGINAL, 1)[0].strip()


def pairs_from_log(path):
    """Recover (vertex, fragment) file names in the order OSG compiled them.

    OSG dumps a program's vertex shader and then its fragment shader, so the
    pairing is simply the sequence.  The "Program attribs" line that follows is
    not a reliable delimiter: a program whose link OSG did not log that way
    would swallow the pair before it.
    """
    order = []
    for line in open(path, errors="replace"):
        m = re.search(r"DUMPSHADER (\S+\.(?:vert|frag))", line)
        if m:
            order.append(os.path.basename(m.group(1)))
    out, vert = [], None
    for name in order:
        if name.endswith(".vert"):
            vert = name                     # a second vertex shader replaces a stray one
        elif vert:
            out.append((vert, name))
            vert = None
    return list(OrderedDict.fromkeys(out))


def sampler_types(src):
    """name -> declared sampler type, so texture() can pick its ES 1.00 name."""
    return {name: kind for kind, name in
            re.findall(r"uniform\s+(?:lowp|mediump|highp\s+)?\s*(sampler\w+)\s+(\w+)\s*;", src)}


def to_es100(src, stage):
    """Rewrite GLSL ES 3.00 into ES 1.00 as far as a mechanical rewrite can."""
    samplers = sampler_types(src)
    src = re.sub(r"^\s*#version\s+300\s+es\s*$", "", src, flags=re.M)

    frag_out = None
    if stage == "frag":
        m = re.search(r"^\s*out\s+\w+\s+(\w+)\s*;\s*$", src, flags=re.M)
        if m:
            frag_out = m.group(1)
            src = src.replace(m.group(0), "")

    if stage == "vert":
        src = re.sub(r"^(\s*)in\s+", r"\1attribute ", src, flags=re.M)
        src = re.sub(r"^(\s*)out\s+", r"\1varying ", src, flags=re.M)
    else:
        src = re.sub(r"^(\s*)in\s+", r"\1varying ", src, flags=re.M)

    if frag_out:
        src = re.sub(r"\b%s\b" % re.escape(frag_out), "gl_FragColor", src)

    # texture(s, ...) picks its ES 1.00 name from how the sampler was declared.
    def texcall(m):
        name = m.group(1)
        kind = samplers.get(name, "sampler2D")
        fn = {"samplerCube": "textureCube", "sampler3D": "texture3D"}.get(kind, "texture2D")
        return "%s(%s" % (fn, name)

    src = re.sub(r"\btexture\s*\(\s*(\w+)", texcall, src)

    def texlodcall(m):
        name = m.group(1)
        kind = samplers.get(name, "sampler2D")
        fn = {"samplerCube": "textureCubeLodEXT"}.get(kind, "texture2DLodEXT")
        return "%s(%s" % (fn, name)

    src = re.sub(r"\btextureLod\s*\(\s*(\w+)", texlodcall, src)

    # ES 1.00 writes the fragment depth through an extension, under its own name.
    src = re.sub(r"\bgl_FragDepth\b", "gl_FragDepthEXT", src)

    # FlightGear's shaders alias the sampling functions behind macros
    # (#define TEXTURE texture) and pick the spelling per profile.  ES 1.00 is
    # one more profile: rewrite the alias rather than every call site.
    src = re.sub(r"(#define\s+\w+\s+)texture\b(?!\s*\()", r"\1texture2D", src)
    src = re.sub(r"(#define\s+\w+\s+)textureLod\b(?!\s*\()", r"\1texture2DLodEXT", src)
    src = re.sub(r"(#define\s+\w+\s+)textureCube\b(?!\s*\()", r"\1textureCube", src)

    # Extensions the rewritten text now depends on.  ES 1.00 has neither 3D
    # textures nor explicit LOD in the fragment stage; both are optional
    # extensions, and whether the device has them is exactly what the probe
    # is there to answer.
    pragmas = []
    if re.search(r"\bsampler3D\b", src):
        pragmas.append("#extension GL_OES_texture_3D : require")
    if re.search(r"\bgl_FragDepthEXT\b", src):
        pragmas.append("#extension GL_EXT_frag_depth : require")
    if re.search(r"\btexture2DLodEXT\b|\btextureCubeLodEXT\b", src):
        pragmas.append("#extension GL_EXT_shader_texture_lod : require")
    if re.search(r"\bdFdx\s*\(|\bdFdy\s*\(|\bfwidth\s*\(", src):
        pragmas.append("#extension GL_OES_standard_derivatives : require")
    if pragmas:
        src = "\n".join(pragmas) + "\n" + src
    return src.strip() + "\n"


def demands(vert, frag):
    """What the pair asks of the driver, in the units ES 2.0 limits are in."""
    varyings = 0
    for decl in re.findall(r"^\s*varying\s+(?:\w+\s+)?(\w+)\s+\w+", vert, flags=re.M):
        varyings += VEC_SIZE.get(decl, 1)
    attribs = len(re.findall(r"^\s*attribute\s", vert, flags=re.M))
    vuni = sum(VEC_SIZE.get(t, 1) * (int(n) if n else 1) for t, n in
               re.findall(r"^\s*uniform\s+(\w+)\s+\w+(?:\[(\d+)\])?\s*;", vert, flags=re.M))
    funi = sum(VEC_SIZE.get(t, 1) * (int(n) if n else 1) for t, n in
               re.findall(r"^\s*uniform\s+(\w+)\s+\w+(?:\[(\d+)\])?\s*;", frag, flags=re.M))
    samplers = len(re.findall(r"uniform\s+sampler\w+", frag))
    gaps = []
    for stage, src in (("vert", vert), ("frag", frag)):
        for pattern, what in ES2_GAPS:
            if re.search(pattern, src):
                gaps.append("%s: %s" % (stage, what))
    return varyings, attribs, vuni, funi, samplers, gaps


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip())
    dump, log, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)

    pairs = pairs_from_log(log)
    if not pairs:
        sys.exit("no vertex/fragment pairs in %s" % log)

    print("%-14s %8s %7s %7s %7s %8s  %s" %
          ("program", "varying", "attrib", "v.unif", "f.unif", "sampler", "gaps"))
    for index, (vfile, ffile) in enumerate(pairs):
        try:
            vsrc = split_dump(open(os.path.join(dump, vfile), errors="replace").read())
            fsrc = split_dump(open(os.path.join(dump, ffile), errors="replace").read())
        except FileNotFoundError as exc:
            print("missing %s" % exc.filename)
            continue
        vert, frag = to_es100(vsrc, "vert"), to_es100(fsrc, "frag")
        name = "p%02d_%s" % (index, os.path.splitext(vfile)[0])
        open(os.path.join(out, name + ".vert"), "w").write(vert)
        open(os.path.join(out, name + ".frag"), "w").write(frag)
        va, at, vu, fu, sa, gaps = demands(vert, frag)
        print("%-14s %8d %7d %7d %7d %8d  %s" %
              (name[:14], va, at, vu, fu, sa, "; ".join(sorted(set(gaps))) or "-"))
    print("\n%d programs written to %s" % (len(pairs), out))


if __name__ == "__main__":
    main()
