#!/usr/bin/env python3
"""Schreibt die Laenderspalte in airports.txt.

Die Liste selbst stammt aus FlightGears apt.dat (make-airports.py im
fgfs-work-Baum, das die Plaetze nach Land in je eine JSON-Datei legt); hier
wird nur nachgetragen, in welchem Land ein Platz liegt - das braucht die
Auswahl im Cockpit, damit man nicht die Kennung raten muss.

Woher das Land kommt: aus dem Dateinamen der fgview-JSON, in der die Kennung
steht (das ist die Zuordnung, die make-airports.py aus der ICAO-Vorsilbe
gebildet hat).  Ausgegeben wird der deutsche Name in Grossbuchstaben ohne
Umlaute - die Schrift im Cockpit ist ein 5x7-Raster und kennt nur ASCII.

    aptcountry.py airports.txt [~/fgfs-work/fgview/airports]

Zeilenformat nachher (Leerzeichen im Land und im Namen als Unterstrich bzw.
als Rest der Zeile):

    Kennung Breite Laenge Hoehe_ft Art Bahnlaenge_m Bahnrichtungen LAND Name
"""

import glob
import json
import os
import sys

# Englisch (so heissen die JSON-Dateien) -> deutsch in ASCII-Grossbuchstaben.
# Lange Namen sind gekuerzt: in der Liste steht eine Zeile je Land, und mehr
# als rund zwanzig Zeichen passen nicht neben die Bilder.
GERMAN = {
    "Afghanistan": "AFGHANISTAN", "Albania": "ALBANIEN", "Algeria": "ALGERIEN",
    "Angola": "ANGOLA", "Anguilla": "ANGUILLA",
    "Antigua and Barbuda": "ANTIGUA/BARBUDA", "Argentina": "ARGENTINIEN",
    "Armenia": "ARMENIEN", "Australia": "AUSTRALIEN", "Austria": "OESTERREICH",
    "Azerbaijan": "ASERBAIDSCHAN", "Bahamas": "BAHAMAS", "Bahrain": "BAHRAIN",
    "Bangladesh": "BANGLADESCH", "Barbados": "BARBADOS", "Belarus": "BELARUS",
    "Belgium": "BELGIEN", "Belize": "BELIZE", "Benin": "BENIN",
    "Bermuda": "BERMUDA", "Bhutan": "BHUTAN", "Bolivia": "BOLIVIEN",
    "Bosnia and Herzegovina": "BOSNIEN-HERZEGOWINA", "Botswana": "BOTSUANA",
    "Brazil": "BRASILIEN",
    "British Indian Ocean Territory": "BRIT INDISCHER OZEAN",
    "British Virgin Islands": "BRIT JUNGFERNINSELN", "Bulgaria": "BULGARIEN",
    "Burkina Faso": "BURKINA FASO", "Burundi": "BURUNDI",
    "Cambodia": "KAMBODSCHA", "Cameroon": "KAMERUN", "Canada": "KANADA",
    "Cape Verde": "KAP VERDE", "Caribbean Netherlands": "KARIB NIEDERLANDE",
    "Cayman Islands": "KAIMANINSELN",
    "Central African Republic": "ZENTRALAFRIKA REP", "Chad": "TSCHAD",
    "Chile": "CHILE", "China": "CHINA", "Colombia": "KOLUMBIEN",
    "Congo": "KONGO", "Cook Islands": "COOKINSELN", "Costa Rica": "COSTA RICA",
    "Croatia": "KROATIEN", "Cuba": "KUBA", "Cyprus": "ZYPERN",
    "Czechia": "TSCHECHIEN", "DR Congo": "DR KONGO", "Denmark": "DAENEMARK",
    "Djibouti": "DSCHIBUTI", "Dominica": "DOMINICA",
    "Dominican Republic": "DOMINIKAN REPUBLIK", "Ecuador": "ECUADOR",
    "Egypt": "AEGYPTEN", "El Salvador": "EL SALVADOR",
    "Equatorial Guinea": "AEQUATORIALGUINEA", "Eritrea": "ERITREA",
    "Estonia": "ESTLAND", "Eswatini": "ESWATINI", "Ethiopia": "AETHIOPIEN",
    "Falkland Islands": "FALKLANDINSELN", "Fiji": "FIDSCHI",
    "Finland": "FINNLAND", "France": "FRANKREICH",
    "French Antilles": "FRANZ ANTILLEN", "French Guiana": "FRANZ GUAYANA",
    "French Polynesia": "FRANZ POLYNESIEN", "Gabon": "GABUN",
    "Gambia": "GAMBIA", "Georgia": "GEORGIEN", "Germany": "DEUTSCHLAND",
    "Ghana": "GHANA", "Gibraltar": "GIBRALTAR", "Greece": "GRIECHENLAND",
    "Greenland": "GROENLAND", "Grenada": "GRENADA", "Guam": "GUAM",
    "Guatemala": "GUATEMALA", "Guinea": "GUINEA",
    "Guinea Bissau": "GUINEA BISSAU", "Guyana": "GUYANA", "Haiti": "HAITI",
    "Honduras": "HONDURAS", "Hong Kong": "HONGKONG", "Hungary": "UNGARN",
    "Iceland": "ISLAND", "India": "INDIEN", "Indonesia": "INDONESIEN",
    "Iran": "IRAN", "Iraq": "IRAK", "Ireland": "IRLAND", "Israel": "ISRAEL",
    "Italy": "ITALIEN", "Ivory Coast": "ELFENBEINKUESTE", "Jamaica": "JAMAIKA",
    "Japan": "JAPAN", "Johnston Atoll": "JOHNSTON ATOLL", "Jordan": "JORDANIEN",
    "Kazakhstan": "KASACHSTAN", "Kenya": "KENIA", "Kiribati": "KIRIBATI",
    "Kosovo": "KOSOVO", "Kuwait": "KUWAIT", "Laos": "LAOS", "Latvia": "LETTLAND",
    "Lebanon": "LIBANON", "Lesotho": "LESOTHO", "Liberia": "LIBERIA",
    "Libya": "LIBYEN", "Lithuania": "LITAUEN", "Luxembourg": "LUXEMBURG",
    "Macau": "MACAU", "Madagascar": "MADAGASKAR", "Malawi": "MALAWI",
    "Malaysia": "MALAYSIA", "Maldives": "MALEDIVEN", "Mali": "MALI",
    "Malta": "MALTA", "Marshall Islands": "MARSHALLINSELN",
    "Mauritania": "MAURETANIEN", "Mauritius": "MAURITIUS", "Mexico": "MEXIKO",
    "Micronesia": "MIKRONESIEN", "Moldova": "MOLDAU", "Mongolia": "MONGOLEI",
    "Montserrat": "MONTSERRAT", "Morocco": "MAROKKO", "Mozambique": "MOSAMBIK",
    "Myanmar": "MYANMAR", "Namibia": "NAMIBIA", "Nauru": "NAURU",
    "Nepal": "NEPAL", "Netherlands": "NIEDERLANDE",
    "New Caledonia": "NEUKALEDONIEN", "New Zealand": "NEUSEELAND",
    "Nicaragua": "NICARAGUA", "Niger": "NIGER", "Nigeria": "NIGERIA",
    "Niue": "NIUE", "North Korea": "NORDKOREA",
    "North Macedonia": "NORDMAZEDONIEN", "Norway": "NORWEGEN", "Oman": "OMAN",
    "Other": "SONSTIGE", "Pakistan": "PAKISTAN", "Palestine": "PALAESTINA",
    "Panama": "PANAMA", "Papua New Guinea": "PAPUA NEUGUINEA",
    "Paraguay": "PARAGUAY", "Peru": "PERU", "Philippines": "PHILIPPINEN",
    "Poland": "POLEN", "Portugal": "PORTUGAL", "Puerto Rico": "PUERTO RICO",
    "Qatar": "KATAR", "Romania": "RUMAENIEN", "Russia": "RUSSLAND",
    "Rwanda": "RUANDA", "Saint Helena": "ST HELENA",
    "Saint Kitts and Nevis": "ST KITTS/NEVIS", "Saint Lucia": "ST LUCIA",
    "Saint Vincent and the Grenadines": "ST VINCENT/GRENADINEN",
    "Samoa": "SAMOA", "Sao Tome and Principe": "SAO TOME/PRINCIPE",
    "Saudi Arabia": "SAUDI ARABIEN", "Senegal": "SENEGAL",
    "Serbia and Montenegro": "SERBIEN/MONTENEGRO", "Seychelles": "SEYCHELLEN",
    "Sierra Leone": "SIERRA LEONE", "Singapore": "SINGAPUR",
    "Slovakia": "SLOWAKEI", "Slovenia": "SLOWENIEN",
    "Solomon Islands": "SALOMONEN", "Somalia": "SOMALIA",
    "South Africa": "SUEDAFRIKA", "South Korea": "SUEDKOREA", "Spain": "SPANIEN",
    "Sri Lanka": "SRI LANKA", "Sudan": "SUDAN", "Suriname": "SURINAME",
    "Sweden": "SCHWEDEN", "Switzerland": "SCHWEIZ", "Syria": "SYRIEN",
    "Taiwan": "TAIWAN", "Tanzania": "TANSANIA", "Thailand": "THAILAND",
    "Timor Leste": "TIMOR LESTE", "Togo": "TOGO",
    "Trinidad and Tobago": "TRINIDAD/TOBAGO", "Tunisia": "TUNESIEN",
    "Turkey": "TUERKEI", "Turks and Caicos": "TURKS/CAICOS",
    "US Virgin Islands": "AM JUNGFERNINSELN", "Uganda": "UGANDA",
    "Ukraine": "UKRAINE", "United Arab Emirates": "VER ARAB EMIRATE",
    "United Kingdom": "GROSSBRITANNIEN", "United States": "USA",
    "United States (Alaska)": "USA ALASKA", "United States (Hawaii)": "USA HAWAII",
    "United States (Midway)": "USA MIDWAY", "United States (Wake)": "USA WAKE",
    "Uruguay": "URUGUAY", "Uzbekistan": "USBEKISTAN", "Vanuatu": "VANUATU",
    "Venezuela": "VENEZUELA", "Vietnam": "VIETNAM",
    "Wallis and Futuna": "WALLIS/FUTUNA", "Yemen": "JEMEN", "Zambia": "SAMBIA",
    "Zimbabwe": "SIMBABWE",
}


def load_map(jsondir):
    """Kennung -> Land, aus je einer Datei Laender.json."""
    out = {}
    for path in sorted(glob.glob(os.path.join(jsondir, "*.json"))):
        stem = os.path.basename(path)[:-5]
        if stem in ("index", "countries"):
            continue
        with open(path) as f:
            data = json.load(f)
        if not isinstance(data, dict):
            continue
        # "United_States__Alaska_" -> "United States (Alaska)"
        name = stem.replace("__", " (").replace("_", " ").strip()
        if name.endswith(" ("):
            name = name[:-2]
        if "(" in name and not name.endswith(")"):
            name = name.rstrip() + ")"
        for rows in data.values():
            if not isinstance(rows, list):
                continue
            for row in rows:
                out[row[0]] = name
    return out


def main(argv):
    apt = argv[1] if len(argv) > 1 else "airports.txt"
    jsondir = argv[2] if len(argv) > 2 else os.path.expanduser(
        "~/fgfs-work/fgview/airports")
    bycode = load_map(jsondir)
    print("%d Kennungen aus %d Laendern in %s" %
          (len(bycode), len(set(bycode.values())), jsondir))

    lines = []
    header = []
    unknown = {}
    missing = 0
    with open(apt) as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split(None, 7)
            if len(parts) < 7:
                continue
            code = parts[0]
            country = bycode.get(code)
            if country is None:
                missing += 1
                country = "Other"
            german = GERMAN.get(country)
            if german is None:
                unknown[country] = unknown.get(country, 0) + 1
                german = country.upper()
            name = parts[7] if len(parts) > 7 else ""
            lines.append(" ".join(parts[:7] + [german.replace(" ", "_"), name]).rstrip())

    header = [
        "# Kennung Breite Laenge Hoehe_ft Art Bahnlaenge_m Bahnrichtungen Land Name",
        "# Art: 0 = Grossflughafen (ab 2500 m), 1 = Kleinflughafen (ab 1000 m), 2 = Flugplatz",
        "# Land: deutsch, Grossbuchstaben ohne Umlaute, Leerzeichen als Unterstrich",
    ]
    with open(apt, "w") as f:
        f.write("\n".join(header + lines) + "\n")
    print("%d Zeilen geschrieben, %d ohne Land" % (len(lines), missing))
    if unknown:
        print("ohne deutschen Namen:",
              ", ".join("%s (%d)" % kv for kv in sorted(unknown.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
