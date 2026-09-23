# Die Antwort vom Gerät

`sgxprobe` auf der Nokia N950 (RM680, Harmattan 1.2), 22.09.2026.

    VENDOR   Imagination Technologies
    RENDERER PowerVR SGX 530
    VERSION  OpenGL ES 2.0          GLSL ES 1.00
    EGL      1.4 build 1.4.14.2514 Nokia

## Die Grenzwerte

| | SGX 530 | FlightGears Bedarf |
|---|---|---|
| `MAX_VARYING_VECTORS` | **8** | Gelände 13, zwei weitere je 10 |
| `MAX_VERTEX_ATTRIBS` | 8 | bis 6 |
| `MAX_VERTEX_UNIFORM_VECTORS` | 128 | bis 29 |
| `MAX_FRAGMENT_UNIFORM_VECTORS` | **64** | eines 63 |
| `MAX_TEXTURE_IMAGE_UNITS` | 8 | bis 7 |
| `MAX_TEXTURE_SIZE` | 2048 | — |

Die Vermutung war 8 varying-Vektoren; es sind genau 8.

## Was die Erweiterungsliste nicht enthält

Vorhanden und nützlich: `GL_OES_standard_derivatives`,
`GL_OES_fragment_precision_high`, `GL_OES_element_index_uint`,
`GL_IMG_texture_compression_pvrtc`, `GL_OES_compressed_ETC1_RGB8_texture`,
`GL_OES_get_program_binary` (übersetzte Programme lassen sich ablegen — bei
diesem Übersetzer ein echtes Argument, siehe unten).

**Nicht vorhanden**, und jede einzelne davon trifft uns:

* `GL_OES_texture_3D` — **vier** unserer zehn Programme benutzen `sampler3D`
  (die Geländemischung). Ohne die Erweiterung gibt es den Typ nicht.
* `GL_EXT_shader_texture_lod` — eines.
* `GL_EXT_frag_depth` — eines.

## Damit steht die Antwort auf die Ausgangsfrage

FlightGears GLES-Shadersatz läuft auf der SGX 530 **nicht**, und zwar nicht
wegen einer Nachlässigkeit in der Umschreibung: drei Programme verlangen mehr
Interpolatoren, als das Gerät besitzt, und vier verlangen 3D-Texturen, die der
Treiber nicht kennt. Das ist keine Übersetzungsfrage mehr, sondern die
Feststellung, dass dieser Renderer für diese Hardware nicht gebaut ist.

Der Weg, für den wir uns vorher entschieden hatten — eigener kleiner Renderer
mit gebackener Szenerie —, ist damit nicht mehr die bequemere Wahl, sondern die
einzige.

## Zwei Nebenbefunde, die für den eigenen Renderer zählen

**Kein ES2-Kontext kann in einen Pbuffer zeichnen.** Von 15 EGL-Konfigurationen
können neun in einen Pbuffer, aber die sind sämtlich ES1 und OpenVG; die sechs
ES2-fähigen bieten nur Fenster und Pixmap. Die Probe zeichnet deshalb in eine
X-Pixmap (`-DSGXPROBE_X11`). Für einen Offscreen-Renderer nach dem Muster von
`harbour-fgview` heißt das: Pixmap oder FBO, kein Pbuffer.

**Der Übersetzer ist langsam.** Der Lauf über die zehn Programme brauchte über
zehn Minuten (bei einem Gerät mit Grundlast). Ein fertiger Renderer sollte seine
Programme über `GL_OES_get_program_binary` ablegen und beim zweiten Start laden.
