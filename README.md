# GENRA Test

Testplugin für neue Instrumente der GENRA-Familie, bevor sie in **GENRA Instruments** einziehen. Es erscheint in
Rack unter der Marke **Genra**, neben GENRA und GENRA Instruments, und funktioniert mit beiden zusammen (Follow GENRA,
MIX ohne Kabel).

**Zurzeit keine Module (2.8.0).** MATH, KURBEL, SWARM, LIED, CANTA, MIAOU, SAURUS, FUMI und KESSEL mit KESSEL FX sind
getestet und mit GENRA 2.38.0 nach **GENRA Instruments** umgezogen (Repo
[Camillowup/Genra-for-Vcv](https://github.com/Camillowup/Genra-for-Vcv), Ordner `instruments/`). Patches mit diesen
Modulen aus GENRA Test: die Module einmal aus GENRA Instruments neu einsetzen. Das alte GENRA-Test-Plugin kann aus dem
Rack-Plugin-Ordner gelöscht werden, sonst stehen die Module doppelt im Browser.

Offenes und Geplantes: `docs/MERKLISTE.md`.

**GENRA CHOP** (Hip-Hop-Beat-Generator mit Sample-Chopping) ist ein eigenes Plugin im eigenen Repo:
https://github.com/Camillowup/Genra-Chop

## Aufbau

| Ordner | Inhalt |
| --- | --- |
| `src/` | das Plugin (`plugin.cpp`, `plugin.hpp`, `shared.cpp`); ein neues Modul bekommt einen eigenen Ordner `src/<modul>/` |
| `genra/`, `res/knob/` | **nicht im Repo:** die gemeinsamen Teile (Knöpfe, Follow GENRA, Link-Hub, JIANT-UI) kommen vor jedem Build frisch aus dem GENRA-Repo (`src/` und `res/knob/` von Camillowup/Genra-for-Vcv, Branch main); von Hand: `make genra-src` |
| `tools/gpanel.py`, `tools/jpanel.py`, `tools/panellib.py` | Werkzeuge für Panels im Look der Vorlagen (GENRA MONO, GRUND, FUNKE, SCHMELZ); ein Modul beschreibt seine Sektionen in `tools/<modul>_panel.py` |

Gebaut wird über GitHub Actions (Windows bei jedem Push, Mac nur beim Release oder von Hand über Actions › Run
workflow). Ein Push auf den Branch `release` veröffentlicht den Release (nur auf ausdrücklichen Wunsch). **Regel:**
Wandert ein Modul nach GENRA Instruments, kommt es im selben Schritt hier heraus (sonst steht es in Rack doppelt).

## Lizenz

GPL-3.0-only (`LICENSE`). Fremdteile: zurzeit keine (siehe `licenses/README.md`).
