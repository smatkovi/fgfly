#!/usr/bin/env python3
"""
acftconv - macht aus einem FlightGear-Flugzeug ein Flugmodell von wenigen
hundert Bytes.

Genommen wird, was das Fliegen bestimmt: Masse, Fluegelflaeche, die
Auftriebs- und Widerstandskurven ueber dem Anstellwinkel, der Beitrag der
Klappen, Leistung und Drehzahlbereich des Motors. Alles andere - Modelle,
Texturen, Klaenge, Systeme, Pruefkarten - bleibt liegen. Bei der c172p sind
das 168 MB im Hangar gegen ein Flugmodell, das in eine Bildschirmzeile passt.

  acftconv.py <verzeichnis|jsbsim.xml|<kennung>-set.xml> [ziel.fdm]

Am besten die `-set.xml` der Spielart, die man will: ein Paket wie
`A320-family` enthaelt fuenf davon, und ohne diese Datei erwischt man
irgendeine. Sie sagt in `sim/aero`, welche Flugmodelldatei dazugehoert.

Gelesen werden JSBSim (`<fdm_config>`, auch mit ausgelagerten Teilen
`<metrics file="Systems/..."/>`) und YASim.
"""
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

LB_TO_KG = 0.45359237
FT2_TO_M2 = 0.09290304
HP_TO_W = 745.7
RHO = 1.225

# Wieviel statischen Schub ein Pferd Leistung an einem Festpropeller macht.
# Fuer die c172p: 160 PS ergeben so 2400 N, gemessen werden rund 2300 N.
NEWTON_PER_HP = 15.0


def text_float(node, default=0.0):
    try:
        return float((node.text or "").strip())
    except (AttributeError, ValueError):
        return default


def find_function(root, *suffixes):
    """Dieselbe Kurve heisst je nach Schreiber anders: die c172p nennt den
    Auftrieb `CLwbh`, ein Verkehrsflugzeug `aero/force/Lift_alpha`."""
    for suffix in suffixes:
        for fn in root.iter("function"):
            if (fn.get("name") or "").endswith(suffix):
                return fn
    return None


def as_degrees(fn, rows):
    """Die Klappentabelle steht mal in Grad (`fcs/flap-pos-deg`), mal als
    Anteil (`-norm`).  Das Flugmodell rechnet in Grad, also umrechnen -
    sonst wirken die Klappen eines Verkehrsflugzeugs nur im ersten Grad."""
    if not rows or fn is None:
        return rows
    var = fn.find(".//independentVar")
    text = (var.text or "") if var is not None and var.text else ""
    if "norm" in text or max(abs(x) for x, _y in rows) <= 1.001:
        return [(x * 30.0, y) for x, y in rows]
    return rows


def read_table(fn, column=0):
    """Eine JSBSim-Tabelle als Liste von (x, y).  Bei mehrspaltigen Tabellen
    zaehlt nur eine Spalte - fuer den Auftrieb die ohne Stroemungsabriss."""
    if fn is None:
        return []
    table = fn.find(".//table")
    if table is None:
        return []
    data = table.find("tableData")
    if data is None or not data.text:
        return []
    rows = []
    lines = [l.split() for l in data.text.strip().splitlines() if l.split()]
    # Eine zweidimensionale Tabelle hat in der ersten Zeile nur die
    # Spaltenwerte, also ein Feld weniger als die folgenden Zeilen.
    two_d = len(lines) > 1 and len(lines[0]) == len(lines[1]) - 1
    for line in lines[1:] if two_d else lines:
        try:
            values = [float(v) for v in line]
        except ValueError:
            continue
        if len(values) < 2:
            continue
        rows.append((values[0], values[1 + column] if two_d else values[1]))
    return rows


def setxml_fdm(path):
    """Aus `<kennung>-set.xml`: die Flugmodelldatei der gewaehlten Spielart."""
    root = ET.parse(path).getroot()
    sim = root.find("sim")
    if sim is None:
        return None
    aero = sim.find("aero")
    aero = (aero.text or "").strip() if aero is not None and aero.text else ""
    if not aero:
        return None
    base = os.path.dirname(os.path.abspath(path))
    for cand in (os.path.join(base, aero + ".xml"),
                 os.path.join(base, "Systems", aero + ".xml"),
                 os.path.join(base, aero)):
        if os.path.isfile(cand):
            return cand
    return None


def inline_includes(root, base_dir, depth=0):
    """JSBSim laesst Teile auslagern: `<metrics file="Systems/a320-metrics"/>`.
    Ohne das Nachladen findet man weder Masse noch Flaeche noch die Beiwerte -
    und nimmt stillschweigend die Vorgabewerte."""
    if depth > 4:
        return
    for child in list(root):
        name = child.get("file")
        if not name:
            inline_includes(child, base_dir, depth + 1)
            continue
        for cand in (os.path.join(base_dir, name),
                     os.path.join(base_dir, name + ".xml")):
            if not os.path.isfile(cand):
                continue
            try:
                sub = ET.parse(cand).getroot()
            except Exception:
                break
            for kid in list(sub):
                child.append(kid)
            for key, value in sub.attrib.items():
                child.set(key, value)
            inline_includes(child, os.path.dirname(cand), depth + 1)
            break


def jsbsim_path(target):
    if os.path.isfile(target):
        # Die Datei aus `sim/aero` kann auch ein YASim-Flugzeug sein - dann
        # gehoert sie nicht hierher.
        head = open(target, errors="replace").read(400)
        return target if "fdm_config" in head else None
    for name in sorted(os.listdir(target)):
        if not name.endswith(".xml"):
            continue
        path = os.path.join(target, name)
        try:
            head = open(path, errors="replace").read(400)
        except OSError:
            continue
        if "fdm_config" in head:
            return path
    return None


def engine_data(aircraft_dir, root):
    """Leistung und Drehzahlen aus der Motordatei, auf die das Flugzeug zeigt.
    Bei einer Turbine gibt es keine Pferdestaerken, sondern Schub in Pfund
    (`<milthrust>`) - und meistens zwei davon."""
    hp, rpm_max, thrust_n = 0.0, 2700.0, 0.0
    for engine in root.findall(".//propulsion/engine"):
        name = engine.get("file") or ""
        # Die Hilfsturbine treibt das Flugzeug nicht an, sie steht im Heck und
        # macht Strom - ihr Schub gehoert nicht dazu.
        if re.search(r"apu|aps\d|gtcp", name, re.I):
            continue
        text = ""
        for folder in ("Engines", "."):
            path = os.path.join(aircraft_dir, folder, name + ".xml")
            if os.path.exists(path):
                text = open(path, errors="replace").read()
                break
        if not text:
            continue
        m = re.search(r"power:\s*([\d.]+)\s*hp", text, re.I)
        if m:
            hp = max(hp, float(m.group(1)))
        m = re.search(r"<maxrpm>\s*([\d.]+)", text)
        if m:
            rpm_max = float(m.group(1))
        m = re.search(r"<milthrust[^>]*>\s*([\d.]+)", text)
        if m:
            thrust_n += float(m.group(1)) * 4.4482
    return hp, rpm_max, thrust_n


def yasim_path(target):
    """Die YASim-Datei: die mit <airplane> darin."""
    if os.path.isfile(target):
        return target if "<airplane" in open(target, errors="replace").read(2000) else None
    for folder in (".", "Systems", "Models"):
        d = os.path.join(target, folder)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".xml"):
                continue
            path = os.path.join(d, name)
            try:
                if "<airplane" in open(path, errors="replace").read(3000):
                    return path
            except OSError:
                continue
    return None


def convert_yasim(path, out_path):
    """YASim beschreibt das Flugzeug geometrisch und loest die Beiwerte beim
    Start selbst.  Wir koennen das nicht - also rechnen wir sie einmal aus,
    und zwar aus den beiden Punkten, die in der Datei stehen: dem Anflug
    (Geschwindigkeit und Anstellwinkel, bei dem der Auftrieb das Gewicht
    traegt) und dem Reiseflug (Leistung, bei der der Schub den Widerstand
    deckt).  Das ist kein YASim, aber es ist aus den Zahlen des Flugzeugs."""
    root = ET.parse(path).getroot()
    name = os.path.splitext(os.path.basename(path))[0]

    mass_lb = float(root.get("mass", 1000))
    for ballast in root.findall(".//ballast"):
        mass_lb += float(ballast.get("mass", 0))
    for prop in root.findall(".//propeller"):
        mass_lb += float(prop.get("mass", 0))
    mass_lb += 170.0                      # ein Mensch sitzt auch drin
    mass_kg = mass_lb * LB_TO_KG

    # Fluegelflaeche und Streckung aus den Flaechenstuecken
    area = 0.0
    span = 0.0
    stall_deg = 15.0
    for wing in root.findall("wing"):
        chord = float(wing.get("chord", 0))
        length = float(wing.get("length", 0))
        taper = float(wing.get("taper", 1))
        area += 2.0 * length * chord * (1.0 + taper) / 2.0
        span += 2.0 * length
        st = wing.find("stall")
        if st is not None:
            stall_deg = float(st.get("aoa", stall_deg))
    if area <= 0.0:
        sys.exit("keine <wing>-Angaben in %s" % path)
    aspect = span * span / area

    approach = root.find("approach")
    cruise = root.find("cruise")
    v_app = float(approach.get("speed", 60)) / 1.94384 if approach is not None else 30.0
    aoa_app = float(approach.get("aoa", 4)) if approach is not None else 4.0
    v_cru = float(cruise.get("speed", 120)) / 1.94384 if cruise is not None else 60.0

    # Auftrieb: der Anflugpunkt ist gegeben - dort traegt der Fluegel genau
    # das Gewicht.  Der Anstieg kommt aus der Streckung (Traglinientheorie).
    cl_app = 2.0 * mass_kg * 9.80665 / (RHO * v_app * v_app * area)
    slope = 2.0 * math.pi * aspect / (aspect + 2.0)        # je Radiant
    cl0 = cl_app - slope * math.radians(aoa_app)

    hp = 0.0
    cruise_hp = 0.0
    for eng in root.iter("piston-engine"):
        hp = max(hp, float(eng.get("eng-power", 0)))
    for prop in root.findall(".//propeller"):
        cruise_hp = max(cruise_hp, float(prop.get("cruise-power", 0)))
    if cruise_hp <= 0.0:
        cruise_hp = hp * 0.65

    # Widerstand: im Reiseflug deckt der Schub den Widerstand.  Daraus faellt
    # der Nullwiderstand, wenn man den induzierten abzieht.
    eta = 0.8
    thrust_cru = cruise_hp * HP_TO_W * eta / max(v_cru, 1.0)
    cl_cru = 2.0 * mass_kg * 9.80665 / (RHO * v_cru * v_cru * area)
    k = 1.0 / (math.pi * aspect * 0.8)
    cd_cru = thrust_cru / (0.5 * RHO * v_cru * v_cru * area)
    cd0 = max(0.015, cd_cru - k * cl_cru * cl_cru)

    # Kurven in Grad, wie sie das Flugmodell erwartet
    cl_rows, cd_rows = [], []
    a = -6.0
    while a <= stall_deg + 12.0:
        if a <= stall_deg:
            cl = cl0 + slope * math.radians(a)
        else:                                   # nach dem Abriss faellt es
            peak = cl0 + slope * math.radians(stall_deg)
            cl = max(0.4 * peak, peak - (a - stall_deg) * 0.06)
        cl_rows.append((a, cl))
        cd_rows.append((a, k * cl * cl + (0.02 if a > stall_deg else 0.0)))
        a += 2.0

    with open(out_path, "w") as f:
        f.write("# fdm1 %s (aus YASim %s)\n" % (name, os.path.basename(path)))
        f.write("name %s\n" % name)
        f.write("mass_kg %.1f\n" % mass_kg)
        f.write("wing_area_m2 %.3f\n" % area)
        f.write("wing_span_m %.3f\n" % span)
        f.write("thrust_max_n %.0f\n" % (hp * NEWTON_PER_HP if hp else 2000.0))
        f.write("rpm_idle 700\n")
        f.write("rpm_max %.0f\n" % max(2400.0, hp and 2700.0))
        f.write("cd0 %.4f\n" % cd0)
        f.write("cd_gear 0.0000\n")
        write_table(f, "cl_alpha", cl_rows)
        write_table(f, "cd_alpha", cd_rows)
        write_table(f, "cl_flap", [(0.0, 0.0), (30.0, 0.25)])
        write_table(f, "cd_flap", [(0.0, 0.0), (30.0, 0.015)])

    print("%s (YASim) -> %s (%d Bytes)" % (name, out_path, os.path.getsize(out_path)))
    print("  Masse %.0f kg, Flaeche %.1f m2, Streckung %.1f, %.0f PS" %
          (mass_kg, area, aspect, hp))
    print("  Anflug %.0f kt bei %.0f Grad -> cl %.2f; Reise %.0f kt, %.0f PS -> cd0 %.4f" %
          (v_app * 1.94384, aoa_app, cl_app, v_cru * 1.94384, cruise_hp, cd0))
    print("  Abriss bei %.0f Grad, %d Stuetzstellen" % (stall_deg, len(cl_rows)))


def convert(target, out_path=None):
    aircraft_dir = target if os.path.isdir(target) else os.path.dirname(target)
    if target.endswith("-set.xml"):
        chosen = setxml_fdm(target)
        if chosen:
            print("  Spielart aus %s: %s" % (os.path.basename(target),
                                             os.path.basename(chosen)))
            target = chosen
        else:
            target = aircraft_dir
    path = jsbsim_path(target)
    if not path:
        ypath = yasim_path(target)
        if ypath:
            out = out_path or os.path.join(os.getcwd(), "aircraft.fdm")
            return convert_yasim(ypath, out)
        sys.exit("weder JSBSim noch YASim in %s" % target)
    root = ET.parse(path).getroot()
    if root.tag != "fdm_config":
        sys.exit("%s ist kein <fdm_config>" % path)
    inline_includes(root, os.path.dirname(os.path.abspath(path)))

    name = root.get("name") or os.path.basename(path)

    area_ft2 = text_float(root.find(".//metrics/wingarea"), 174.0)
    span_ft = text_float(root.find(".//metrics/wingspan"), 35.8)

    mass_lb = text_float(root.find(".//mass_balance/emptywt"), 1500.0)
    for pm in root.findall(".//mass_balance/pointmass"):
        mass_lb += text_float(pm.find("weight"), 0.0)

    cl_alpha = read_table(find_function(root, "CLwbh", "Lift_alpha"), column=0)
    cd_alpha = read_table(find_function(root, "CDwbh", "Drag_alpha"), column=0)
    cl_flap_fn = find_function(root, "CLDf", "Lift_flap")
    cd_flap_fn = find_function(root, "CDDf", "Drag_flap")
    cl_flap = as_degrees(cl_flap_fn, read_table(cl_flap_fn))
    cd_flap = as_degrees(cd_flap_fn, read_table(cd_flap_fn))

    cd0_fn = find_function(root, "CDo", "Drag_minimum")
    cd0_table = read_table(cd0_fn)
    cd0 = cd0_table[0][1] if cd0_table else 0.027

    hp, rpm_max, thrust_n = engine_data(aircraft_dir, root)

    out = out_path or os.path.join(os.getcwd(), (root.get("name") or "aircraft") + ".fdm")
    with open(out, "w") as f:
        f.write("# fdm1 %s (aus %s)\n" % (name, os.path.basename(path)))
        f.write("name %s\n" % name)
        f.write("mass_kg %.1f\n" % (mass_lb * LB_TO_KG))
        f.write("wing_area_m2 %.3f\n" % (area_ft2 * FT2_TO_M2))
        f.write("wing_span_m %.3f\n" % (span_ft * 0.3048))
        f.write("thrust_max_n %.0f\n" %
                (thrust_n if thrust_n else (hp * NEWTON_PER_HP if hp else 2600.0)))
        f.write("rpm_idle %.0f\n" % 700.0)
        f.write("rpm_max %.0f\n" % rpm_max)
        f.write("cd0 %.4f\n" % cd0)
        # Anstellwinkel in Grad, damit das Flugmodell nicht umrechnen muss
        write_table(f, "cl_alpha", [(a * 180.0 / 3.14159265, v) for a, v in cl_alpha])
        write_table(f, "cd_alpha", [(a * 180.0 / 3.14159265, v) for a, v in cd_alpha])
        write_table(f, "cl_flap", cl_flap)
        write_table(f, "cd_flap", cd_flap)

    print("%s -> %s (%d Bytes)" % (name, out, os.path.getsize(out)))
    print("  Masse %.0f kg, Flaeche %.1f m2, %s, cd0 %.4f" %
          (mass_lb * LB_TO_KG, area_ft2 * FT2_TO_M2,
           ("%.0f kN Schub" % (thrust_n / 1000.0)) if thrust_n else ("%.0f PS" % hp),
           cd0))
    print("  Tabellen: cl_alpha %d, cd_alpha %d, cl_flap %d, cd_flap %d Punkte" %
          (len(cl_alpha), len(cd_alpha), len(cl_flap), len(cd_flap)))


def write_table(f, key, rows):
    if not rows:
        return
    f.write("%s %d" % (key, len(rows)))
    for x, y in rows:
        f.write(" %.4f %.4f" % (x, y))
    f.write("\n")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip())
    convert(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
