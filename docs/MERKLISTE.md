# Merkliste GENRA Test (offen)

Nur Offenes für dieses Testplugin. Die grosse Merkliste der Familie liegt im GENRA-Repo (`docs/MERKLISTE.md` in
Camillowup/Genra-for-Vcv), die Regeln fürs Aussehen in dessen `docs/DESIGN.md`. Gebaut wird erst, wenn ausdrücklich
„bauen“ kommt; vorher wird jedes Panel als Entwurf gezeigt.

## Regel: Look wie die Vorlagen

Alle Module sollen aussehen wie **GENRA MONO, GRUND, FUNKE und SCHMELZ**: Preset-Feld im Kopf (Mausrad wechselt),
Display über die volle Breite, Sektionen mit Farblinie, der wichtigste Regler gross mit gepunktetem Ring in der
Akzentfarbe, Werte im Display statt unter den Reglern, alle Buchsen unten in zwei Reihen (GENRA-Leiste und CV oben,
Ausgänge auf farbigen Platten unten). Tasten gross genug (wie im MATH-Muster), Schriften überlappen nie, Panels so
schmal wie möglich. Alle Dropdown-Felder wechseln auch per Mausrad.

## Umstellung der bestehenden Module

Umgestellt (gebaut, in Rack zu testen): MATH 22 HP, SWARM 23 HP, SAURUS 22 HP, KURBEL 24 HP, MIAOU 19 HP,
FUMI 25 HP, CANTA 26 HP. Panels kommen aus `tools/gpanel.py` (die `tools/<modul>_panel.py` beschreiben nur die
Sektionen). LIED war schon im Vorlagen-Look (25 HP, eigener Generator) und hat jetzt das Mausrad auf allen Feldern.
Alle Parameter und Buchsen sind gleich geblieben, alte Patches laden weiter. In Rack prüfen:
- dass keine Schrift überlappt (die Display-Texte sind dynamisch),
- MIAOU: der Klang wird jetzt im Kopf-Feld gewählt (die SOUND-Lämpchen sind weg),
- CANTA: das Silben-Rad ist weg, gross steht nur noch die Silbe, die klingt; die Textzeile sitzt rechts im Display.

## KESSEL / KESSEL FX (neu in 2.6.0)

Gebaut und in Rack zu testen. Schon im neuen Look. Offen, falls gewünscht:
- Kits beim Anhören nachstimmen (Lautstärke, Charakter der Stile).
- Ein MIDI-Eingang direkt im Modul (wie bei SCHLAG) wäre möglich.
