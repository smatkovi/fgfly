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
        f.write("# fdm2 %s (aus YASim %s)\n" % (name, os.path.basename(path)))
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
        # Und darunter die Geometrie selbst: dieselbe Datei fuettert damit
        # beide Modelle -- das alte liest die Tabellen und ueberliest den
        # Rest, das neue nimmt die Flaechen.
        geo = yasim_geometry(root, mass_kg)
        if geo:
            getrimmt = trimmen(geo[0], mass_kg, v_cru)
            write_geometry(f, geo, mass_kg)
            # Die Leistung selbst, nicht nur ein daraus geschaetzter Schub:
            # ein Propeller zieht beim Anrollen ein Vielfaches dessen, was
            # er bei Reisegeschwindigkeit zieht, und das entscheidet ueber
            # den Startlauf.
            radius = 0.0
            for prop in root.findall(".//propeller"):
                radius = max(radius, _f(prop, "radius", 0.0))
            if hp > 0.0:
                f.write("leistung_w %.0f\n" % (hp * 745.7))
                f.write("propeller_r %.2f\n" % (radius if radius > 0.1 else 0.9))

    print("%s (YASim) -> %s (%d Bytes)" % (name, out_path, os.path.getsize(out_path)))
    print("  Masse %.0f kg, Flaeche %.1f m2, Streckung %.1f, %.0f PS" %
          (mass_kg, area, aspect, hp))
    print("  Anflug %.0f kt bei %.0f Grad -> cl %.2f; Reise %.0f kt, %.0f PS -> cd0 %.4f" %
          (v_app * 1.94384, aoa_app, cl_app, v_cru * 1.94384, cruise_hp, cd0))
    print("  Abriss bei %.0f Grad, %d Stuetzstellen" % (stall_deg, len(cl_rows)))
    if geo:
        print("  Geometrie: %d Flaechen, %d Beine, Schwerpunkt aus der %s"
              % (len(geo[0]), len(geo[1]), geo[5]))
        if geo[4]:
            print("  Traegheit %.0f / %.0f / %.0f kg m2" % geo[4])
        print("  Neutralpunkt liegt %.2f m hinter dem Schwerpunkt%s"
              % (-geo[6], "" if geo[6] < 0 else "  -- INSTABIL"))
        if getrimmt:
            print("  Getrimmt fuer Reiseflug: Anstellwinkel %.1f Grad, "
                  "Leitwerk %.1f Grad" % getrimmt)



# ---------------------------------------------------------------------------
# Die Geometrie durchreichen, statt sie zu Beiwerten zusammenzurechnen.
#
# YASim beschreibt das Flugzeug so, wie das Blattelementverfahren es braucht:
# Flaechen mit Lage, Tiefe, Laenge, Zuspitzung, Einstellwinkel, Schraenkung,
# V-Stellung, Pfeilung, Woelbung und Abrissverhalten.  Genau das schreiben
# wir jetzt zusaetzlich in die .fdm-Datei -- die Beiwertetabellen bleiben
# daneben stehen, damit dieselbe Datei auch das alte Modell noch fuettert.
#
# Zwei Dinge muessen dabei umgerechnet werden:
#
# 1. **Die Achsen.**  YASim zaehlt x nach vorn, y nach rechts, z nach *oben*
#    -- man sieht es an den Fahrwerken, deren z negativ ist, weil das Rad
#    unten haengt.  Das Flugmodell zaehlt z nach unten, also Vorzeichen
#    drehen.
#
# 2. **Der Ursprung.**  YASim-Koordinaten haengen an einem beliebigen Punkt
#    des 3D-Modells, nicht am Schwerpunkt; den rechnet YASim aus den Massen
#    aus, die wir nicht haben.  Also wird er aus der Geometrie geschaetzt:
#    Der Neutralpunkt ist der flaechengewichtete Angriffspunkt aller
#    waagrechten Flaechen, und der Schwerpunkt liegt ein Stueck davor --
#    acht Prozent der mittleren Tiefe, ein ueblicher Stabilitaetsabstand.
#    Das ist geschaetzt, aber aus den Zahlen des Flugzeugs geschaetzt, und
#    es ist die einzige Stelle, an der geschaetzt wird.

def _f(node, name, default=0.0):
    try:
        return float(node.get(name, default))
    except (TypeError, ValueError):
        return default


def _stall(node):
    st = node.find("stall")
    if st is None:
        return 16.0, 4.0, 1.5
    return _f(st, "aoa", 16.0), _f(st, "width", 4.0), _f(st, "peak", 1.5)


def _controls(node):
    """Welches Ruder auf welcher Flaeche sitzt -- YASim sagt es ueber die
    Achse, an die der Klappensatz gehaengt ist."""
    anteile = {"flap": 0.0, "aileron": 0.0, "elevator": 0.0, "rudder": 0.0}
    for ci in node.findall("control-input"):
        axis = ci.get("axis", "")
        ctrl = ci.get("control", "")
        if not ctrl.startswith("FLAP"):
            continue
        klappe = node.find(ctrl.lower())
        if klappe is None:
            continue
        # Spannweitenanteil mal ein Viertel der Tiefe: so gross ist ein Ruder
        # ueblicherweise, und genauer sagt es die Datei nicht.
        anteil = max(0.0, _f(klappe, "end", 1.0) - _f(klappe, "start", 0.0)) * 0.25
        # `invert` sagt, in welche Richtung das Ruder ausschlaegt.  Beim
        # Enten-Flugzeug sitzt das Hoehenruder vorn: Ziehen heisst dort
        # **mehr** Auftrieb, nicht weniger.  Ohne diese Zeile laesst sich
        # ein Long-EZ nicht hochziehen, sondern nur druecken.
        if ci.get("invert", "false").lower() in ("true", "1"):
            anteil = -anteil
        for name, wort in (("aileron", "aileron"), ("elevator", "elevator"),
                           ("rudder", "rudder"), ("flap", "flaps")):
            if wort in axis and abs(anteil) > abs(anteile[name]):
                anteile[name] = anteil
    return anteile


def yasim_geometry(root, mass_kg=0.0):
    """Liefert (flaechen, beine, flaeche_m2, spannweite_m) oder None."""
    flaechen = []
    for tag in ("wing", "mstab", "hstab", "vstab"):
        for node in root.findall(tag):
            chord = _f(node, "chord")
            length = _f(node, "length")
            if chord <= 0.0 or length <= 0.0:
                continue
            aoa, width, peak = _stall(node)
            c = _controls(node)
            flaechen.append({
                "tag": tag,
                "x": _f(node, "x"), "y": _f(node, "y"), "z": _f(node, "z"),
                "chord": chord, "length": length,
                "taper": _f(node, "taper", 1.0),
                "incidence": _f(node, "incidence"),
                "twist": _f(node, "twist"),
                "dihedral": _f(node, "dihedral"),
                "sweep": _f(node, "sweep"),
                "camber": _f(node, "camber"),
                "stall_aoa": aoa, "stall_width": width, "stall_peak": peak,
                "flap": c["flap"], "aileron": c["aileron"],
                "elevator": c["elevator"], "rudder": c["rudder"],
                "vertical": 1 if tag == "vstab" else 0,
                # Ein Seitenleitwerk steht in der Datei so oft, wie es es
                # gibt (Winglets zweimal); alles andere spiegelt YASim selbst.
                "mirror": 0 if tag == "vstab" else 1,
            })
    if not flaechen:
        return None

    beine = []
    for node in root.findall("gear"):
        lenkbar, gebremst = 0.0, 0.0
        for ci in node.findall("control-input"):
            ctrl = ci.get("control", "")
            if ctrl == "STEER":
                lenkbar = 1.0
            if ctrl == "BRAKE":
                gebremst = 1.0
        beine.append({
            "x": _f(node, "x"), "y": _f(node, "y"), "z": _f(node, "z"),
            "spring": _f(node, "spring", 1.0),
            "damp": _f(node, "damp", 1.0),
            "compression": _f(node, "compression", 0.2),
            "steer": lenkbar, "brake": gebremst,
        })

    area, span = 0.0, 0.0
    for s in flaechen:
        if s["vertical"]:
            continue
        halb = s["length"] * s["chord"] * (1.0 + s["taper"]) / 2.0
        area += halb * (2 if s["mirror"] else 1)
        if s["tag"] in ("wing", "mstab"):
            span = max(span, 2.0 * (abs(s["y"]) + s["length"]))

    # Erst versuchen, den Schwerpunkt aus der Masseverteilung zu rechnen --
    # das ist der ehrliche Weg und liefert die Traegheit gleich mit.
    traegheit = None
    schwer = None
    if mass_kg > 0.0:
        erg = massenpunkte(root, flaechen, mass_kg)
        if erg:
            schwer, traegheit = erg

    # Der Neutralpunkt: der Punkt, um den das Nickmoment sich nicht mehr
    # aendert, wenn der Anstellwinkel steigt.  Gewichtet wird mit Flaeche
    # **mal Auftriebsanstieg** -- ein kurzes Leitwerk kleiner Streckung
    # traegt je Quadratmeter deutlich weniger bei als der Fluegel, und wer
    # nur die Flaeche nimmt, schiebt den Punkt zu weit nach hinten.  Genau
    # das ist passiert: die AG-14 bekam ihren Schwerpunkt hinter den
    # Neutralpunkt und taumelte nach zehn Sekunden.
    summe, gewicht, tiefe = 0.0, 0.0, 0.0
    for s in flaechen:
        if s["vertical"]:
            continue
        a = s["length"] * s["chord"] * (1.0 + s["taper"]) / 2.0 * (2 if s["mirror"] else 1)
        mittel = s["chord"] * (1.0 + s["taper"]) / 2.0
        streckung = max(1.0, (2.0 * s["length"] if s["mirror"] else s["length"]) / max(0.01, mittel))
        anstieg = 2.0 * math.pi * streckung / (streckung + 2.0)
        w = a * anstieg
        summe += w * (s["x"] - 0.25 * s["chord"])   # Angriffspunkt bei t/4
        gewicht += w
        tiefe += w * s["chord"]
    if gewicht <= 0.0:
        return None
    x_np = summe / gewicht
    mittlere_tiefe = tiefe / gewicht
    # Zwoelf Prozent Stabilitaetsmass: so viel liegt der Schwerpunkt vor dem
    # Neutralpunkt.  Bei einem echten Flugzeug sind es 5 bis 15 Prozent.
    x_cg = x_np + 0.12 * mittlere_tiefe

    groesste = max((s for s in flaechen if not s["vertical"]),
                   key=lambda s: s["length"] * s["chord"])
    z_cg = groesste["z"]
    y_cg = 0.0
    quelle = "Neutralpunkt"
    if schwer:
        x_cg, y_cg, z_cg = schwer
        quelle = "Masseverteilung"

    for s in flaechen:
        s["x"] -= x_cg
        s["y"] -= y_cg
        s["z"] = -(s["z"] - z_cg)
    for b in beine:
        b["x"] -= x_cg
        b["y"] -= y_cg
        b["z"] = -(b["z"] - z_cg)
    return flaechen, beine, area, span, traegheit, quelle, x_np - x_cg




def massenpunkte(root, flaechen, mass_kg):
    """Der Schwerpunkt aus der Masseverteilung, nicht aus einer Faustregel.

    YASim macht es genauso: Die Leermasse wird ueber die Bauteile verteilt --
    Rumpf nach Volumen, Flaechen nach Flaeche mal Tiefe --, dazu kommen die
    Punktmassen, die in der Datei stehen (Ballast, Propeller, Tanks).  Der
    Schwerpunkt ist dann `Summe m*r / Summe m`, und aus derselben Summe
    fallen die Traegheitsmomente heraus: `Ixx = Summe m*(y^2+z^2)` und so
    fort.

    Das ist der Unterschied zwischen "geschaetzt" und "gerechnet": Vorher
    stand der Schwerpunkt dort, wo eine angenommene Stabilitaetsreserve ihn
    hinlegte, und die Traegheit kam aus Spannweite mal Daumen.  Jetzt
    kommen beide aus den Zahlen des Flugzeugs.

    Punkte kommen in YASim-Koordinaten heraus (x vorn, y rechts, z oben).
    """
    punkte = []      # (masse_kg, x, y, z)
    fest = 0.0

    def zu(masse, x, y, z):
        punkte.append((masse, x, y, z))

    # 1. Punktmassen, die dastehen.  Ballast in Pfund, wie alles bei YASim.
    for b in root.findall("ballast"):
        m = _f(b, "mass") * LB_TO_KG
        if m != 0.0:
            zu(m, _f(b, "x"), _f(b, "y"), _f(b, "z"))
    for w in root.findall(".//weight"):
        m = _f(w, "mass-lbs") * LB_TO_KG
        if m <= 0.0 and "pilot" in w.get("mass-prop", ""):
            # Der Pilot haengt in YASim an einer Eigenschaft, nicht an einer
            # Zahl -- seine Masse steht also nirgends in der Datei.  Sie
            # gehoert trotzdem dazu, und vor allem gehoert sie **dorthin**,
            # wo er sitzt: bei der AG-14 einen Meter vor dem Fluegel.  Ohne
            # ihn lag der Schwerpunkt hinter dem Neutralpunkt, und das
            # Flugzeug war nicht zu halten.
            m = 170.0 * LB_TO_KG
        if m > 0.0:
            zu(m, _f(w, "x"), _f(w, "y"), _f(w, "z"))
    for t in root.findall(".//tank"):
        # Tanks sind im Leerzustand leer; ein Fuenftel Sprit ist die
        # Annahme, mit der YASim auch den Anflug rechnet.
        m = _f(t, "capacity") * LB_TO_KG * 0.2
        if m > 0.0:
            zu(m, _f(t, "x"), _f(t, "y"), _f(t, "z"))
    for p in root.findall(".//propeller"):
        m = _f(p, "mass") * LB_TO_KG
        if m > 0.0:
            zu(m, _f(p, "x"), _f(p, "y"), _f(p, "z"))
    fest = sum(m for m, _x, _y, _z in punkte)

    # 2. Die Struktur: was uebrigbleibt, nach Volumen verteilt.
    struktur = []
    for fus in root.findall("fuselage"):
        ax, ay, az = _f(fus, "ax"), _f(fus, "ay"), _f(fus, "az")
        bx, by, bz = _f(fus, "bx"), _f(fus, "by"), _f(fus, "bz")
        laenge = math.sqrt((bx - ax) ** 2 + (by - ay) ** 2 + (bz - az) ** 2)
        breite = _f(fus, "width", 1.0)
        if laenge <= 0.0 or breite <= 0.0:
            continue
        # In drei Stuecke, damit die Traegheit um die Querachse stimmt --
        # ein Punkt in der Mitte wuesste nichts von der Laenge.
        for anteil in (0.2, 0.5, 0.8):
            struktur.append((laenge * breite * breite / 3.0,
                             ax + (bx - ax) * anteil,
                             ay + (by - ay) * anteil,
                             az + (bz - az) * anteil))
    for s in flaechen:
        mittel = s["chord"] * (1.0 + s["taper"]) / 2.0
        seiten = (1, -1) if s["mirror"] else (1,)
        for seite in seiten:
            # Drei Stuecke je Haelfte: die Masse liegt aussen genauso wie
            # innen, und genau davon lebt das Traegheitsmoment ums Rollen.
            for anteil in (0.17, 0.5, 0.83):
                laenge = s["length"] * anteil
                y = s["y"] * seite + (0.0 if s["vertical"] else seite * laenge)
                z = s["z"] + (laenge if s["vertical"] else 0.0)
                struktur.append((mittel * s["length"] / 3.0 * mittel,
                                 s["x"] - 0.4 * s["chord"], y, z))
    summe_struktur = sum(v for v, _x, _y, _z in struktur)
    rest = max(0.0, mass_kg - fest)
    if summe_struktur > 0.0 and rest > 0.0:
        for v, x, y, z in struktur:
            zu(rest * v / summe_struktur, x, y, z)

    gesamt = sum(m for m, _x, _y, _z in punkte)
    if gesamt <= 0.0:
        return None
    cx = sum(m * x for m, x, _y, _z in punkte) / gesamt
    cy = sum(m * y for m, _x, y, _z in punkte) / gesamt
    cz = sum(m * z for m, _x, _y, z in punkte) / gesamt

    ixx = iyy = izz = 0.0
    for m, x, y, z in punkte:
        dx, dy, dz = x - cx, y - cy, z - cz
        ixx += m * (dy * dy + dz * dz)
        iyy += m * (dx * dx + dz * dz)
        izz += m * (dx * dx + dy * dy)
    # Punktmassen allein unterschaetzen die Traegheit eines ausgedehnten
    # Koerpers; ein Viertel Zuschlag ist die uebliche Korrektur.
    return (cx, cy, cz), (ixx * 1.25, iyy * 1.25, izz * 1.25)

def trimmen(flaechen, mass_kg, v_cru, rho=1.225):
    """Das Leitwerk so einstellen, dass das Flugzeug im Reiseflug von selbst
    geradeaus fliegt.

    Das ist der Schritt, den YASim beim Laden rechnet und den ein
    Geometriemodell braucht: Ohne ihn traegt der Fluegel vor dem
    Schwerpunkt, niemand haelt dagegen, und das Flugzeug zieht die Nase
    hoch, bis es ueberzieht -- im Versuch nach genau einer Sekunde.

    Zwei Gleichungen, zwei Unbekannte: Die Auftriebe muessen das Gewicht
    tragen, und ihre Momente um den Schwerpunkt muessen sich aufheben.
    Daraus folgt, wieviel das Leitwerk tragen muss, und daraus sein
    Einstellwinkel.
    """
    waagrecht = [s for s in flaechen if not s["vertical"]]
    if len(waagrecht) < 2:
        return
    haupt = max(waagrecht, key=lambda s: s["length"] * s["chord"])
    leitwerke = [s for s in waagrecht if s is not haupt]

    def kennwerte(s):
        mittel = s["chord"] * (1.0 + s["taper"]) / 2.0
        spann = (2.0 * s["length"]) if s["mirror"] else s["length"]
        flaeche = mittel * spann
        streckung = max(1.0, spann / max(0.01, mittel))
        anstieg = 2.0 * math.pi * streckung / (streckung + 2.0)
        # Der Angriffspunkt liegt bei einem Viertel der Tiefe.
        d = s["x"] - 0.25 * s["chord"]
        return flaeche, anstieg, d

    S_w, a_w, d_w = kennwerte(haupt)
    S_t, a_t, d_t = 0.0, 0.0, 0.0
    for s in leitwerke:
        f, an, d = kennwerte(s)
        S_t += f
        a_t = an if a_t == 0.0 else (a_t + an) / 2.0
        d_t += f * d
    if S_t <= 0.0:
        return
    d_t /= S_t
    if abs(d_t - d_w) < 0.05:
        return

    W = mass_kg * 9.80665
    q = 0.5 * rho * v_cru * v_cru
    if q < 1.0:
        return
    nenner = 1.0 - (d_w / d_t if abs(d_t) > 1e-6 else 0.0)
    if abs(nenner) < 1e-6:
        return
    L_w = W / nenner
    L_t = -L_w * d_w / d_t

    # Anstellwinkel des Fluegels daraus, dann der des Leitwerks.
    cl_w = L_w / (q * S_w)
    alpha = cl_w / a_w - math.radians(haupt["incidence"]) - haupt["camber"]
    cl_t = L_t / (q * S_t)
    i_t = cl_t / a_t - alpha
    for s in leitwerke:
        s["incidence"] = math.degrees(i_t)
    return math.degrees(alpha), math.degrees(i_t)

def write_geometry(f, geo, mass_kg):
    flaechen, beine, area, span, traegheit, quelle, abstand = geo
    f.write("geometrie 1" + "\n")
    if traegheit:
        f.write("inertia %.0f %.0f %.0f" % traegheit + "\n")
    for s in flaechen:
        f.write(("flaeche %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %.2f %.2f "
                 "%.3f %.1f %.1f %.2f %.2f %.2f %.2f %.2f %d %d" + "\n") %
                (s["x"], s["y"], s["z"], s["chord"], s["length"], s["taper"],
                 s["incidence"], s["twist"], s["dihedral"], s["sweep"],
                 s["camber"], s["stall_aoa"], s["stall_width"], s["stall_peak"],
                 s["flap"], s["aileron"], s["elevator"], s["rudder"],
                 s["mirror"], s["vertical"]))
    for b in beine:
        # YASim gibt Feder und Daempfung als Faktoren; der Grundwert ergibt
        # sich daraus, dass die Feder das Flugzeug auf dem Federweg traegt.
        weg = max(0.05, b["compression"])
        feder = b["spring"] * mass_kg * 9.80665 / weg
        daempfer = b["damp"] * 0.5 * math.sqrt(feder * mass_kg)
        f.write(("bein %.3f %.3f %.3f %.0f %.0f %.1f %.1f" + "\n") %
                (b["x"], b["y"], b["z"], feder, daempfer, b["steer"], b["brake"]))

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
        if thrust_n:
            # Eine Turbine verliert mit der Fahrt kaum Schub - der Propeller
            # schon.  Das Flugmodell braucht den Unterschied.
            f.write("jet 1\n")
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
