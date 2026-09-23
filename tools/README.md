# Herunterladen auf dem N950

## aria2c: gebaut, läuft aber nicht

`build-aria2.sh` (auf dem Baurechner) baut aria2 1.37.0 für Harmattan:

* erst **OpenSSL 1.1.1w** statisch, weil die TerraSync-Spiegel HTTPS sprechen
  und das OpenSSL des Geräts 0.9.8r ist (kein TLS 1.2);
* dann aria2 ohne BitTorrent und Metalink — das spart libxml2, gmp und nettle —
  mit `ARIA2_STATIC=yes`, hartem Gleitkomma und Harmattans Ladernamen.

Heraus kommt **ein einziges statisches Programm von 4,8 MB**, das sich einfach
mitliefern lässt. Zwei Stellen mussten nachgebessert werden:

* `-lpthread` von Hand ins `LIBS` — glibc 2.10 hat pthread noch nicht in libc;
* `HAVE_FALLOCATE` aus `config.h` genommen: configure findet die Erklärung im
  Header, aber `fallocate64` steht nicht in der libc des Sysroots.

**Auf dem Gerät startet es und bleibt dann stehen.** `aria2c --version`
antwortet sofort; jeder Download hängt nach `Downloading 1 item(s)` — auch
über **reines HTTP** (also ohne TLS), auch mit `--event-poll=select`, auch
gegen eine nackte IP-Adresse (also ohne DNS). Damit sind Zertifikate, der
Namensdienst und die Ereignisschleife als Ursache ausgeschlossen; was bleibt,
wäre ein Bau mit `--enable-debug` und Geduld. Der Gewinn wäre klein, denn:

## Was es auf dem Gerät schon gibt: `/opt/wunderw`

Der entscheidende Fund. Dort liegt eine moderne Werkzeugkiste:

    /opt/wunderw/bin/python3   Python 3.11.3, mit OpenSSL 1.1.1w
    /opt/wunderw/bin/wget      GNU Wget 1.21.4, +https, gegen libssl 1.1
    /opt/wunderw/bin/openssl   dazu perl 5.38 und 47 weitere

Damit ist die TLS-Frage erledigt — **und mehr**: `fgfs-scenery`, unser
TerraSync-Holer aus `~/fgfs-work`, läuft damit **unverändert auf der N950**.
Spiegelsuche, Indexlauf über HTTPS, Vergleich der SHA1-Summen, alles.

Nur der Download ging vorher über aria2c. Dafür hat `fgfs-scenery` jetzt
`--downloader python`: ein paar Fäden, jeder holt eine Datei, und bei einem
toten Spiegel geht es zum nächsten. Gemessen auf dem Gerät:

    143 geladen, 0 fehlgeschlagen, 55,4 MB in 93 s (608 KB/s)

## Wohin die Daten gehören

    /home            2,0 GB,  463 MB frei   ext4    <- Programme
    /home/user/MyDocs 8,8 GB,  2,3 GB frei   vfat    <- Daten

MyDocs ist **noexec** eingehängt: Programme müssen in `/home/user` bleiben,
Daten gehören nach MyDocs. vfat heißt außerdem: keine Gross-/Kleinschreibung —
für TerraSync unproblematisch, die Namen sind Zahlen.

Und eine Falle, die eine halbe Stunde gekostet hat: **`/tmp` ist ein 4-MB-tmpfs
und steht permanent auf 100 %.** Was dorthin geschrieben wird, hat hinterher
0 Bytes — ohne Fehlermeldung, ohne Rückgabewert ungleich null. Beim Backen
also immer nach MyDocs schreiben.

## Was ohne wunderw funktioniert

    System-curl, reines HTTP:  1 247 327 Bytes in 2 s   (~600 KB/s)
    vier curl gleichzeitig:    läuft

**Die TerraSync-Spiegel geben ihre Dateien auch über reines HTTP heraus** —
damit spielt das alte OpenSSL des Geräts keine Rolle mehr, und der eigene
`curl` reicht. Parallel geht es mit mehreren curl-Prozessen: die Kacheln sind
einzelne Dateien, das teilt sich von selbst auf.

Gerechnet: eine 1-Grad-Schachtel sind rund 30 MB, also etwa **eine Minute
Laden** und danach **zwei bis drei Minuten Backen** auf dem Gerät.

`cacert.pem` (von curl.se) liegt hier für den Fall, dass doch einmal HTTPS
gebraucht wird — die Wurzelzertifikate des Geräts sind von 2011.
