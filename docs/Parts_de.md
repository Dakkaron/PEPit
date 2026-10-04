# Benötigte Werkzeuge

- Schraubenzieher
- Lötkolben mit Lötzinn
- Werkzeug zum Abisolieren von Kabeln
- 3D-Drucker (optional)
- PC mit Windows (Andere Betriebssysteme sind möglich, werden aber in diesem Guide aktuell nicht behandelt)
- SD-Karte zu USB Adapter (falls man im PC keinen SD-Karten-Slot hat)
- Patafix oder doppelseitiges Klebeband

# Benötigte Bauteile

## Elektronische Bauteile

- LILLYGO T-HMI: https://lilygo.cc/products/t-hmi
- Luftdrucksensor: https://de.aliexpress.com/item/1005006421627550.html

![Luftdrucksensor](https://raw.githubusercontent.com/Dakkaron/T-HMI-PEPmonitor/refs/heads/main/docs/images/airpressuresensor.png)
- 1S Akku mit PH2.0 Anschluss, mindestens 200mA (https://de.aliexpress.com/item/1005007256417369.html)
- MicroSD-Karte mit mindestens 1GB und maximal 32GB.
- M5stack Joystick2

## Schrauben

- 4x M3x25 Schrauben
- 4x M3 Muttern, viereckig
- 4x M2x10 Schrauben

## Sonstiges

- Doppelseitiges Klebeband, am Besten Tesa Powerstrips Small

## 3D-Druck

- Obere Hälfte: (https://github.com/Dakkaron/T-HMI-PEPmonitor/blob/main/3DPrint/T_HMI_PEP_case_v3.2_top.stl)
- Untere Hälfte: (https://github.com/Dakkaron/T-HMI-PEPmonitor/blob/main/3DPrint/T_HMI_PEP_case_v3.2_bottom_with_tpu.stl)
- Schlauch-Adapter: (https://github.com/Dakkaron/T-HMI-PEPmonitor/blob/main/3DPrint/PEP_adapter.stl)

Der Schlauchadapter muss mit flexiblem Material (z.B. TPU- oder TPE-Filament) gedruckt werden.

Sollte kein 3D-Drucker zur Verfügung stehen, bietet sich der Druckservice von [JLC3DP](https://jlc3dp.com) an.

### Anleitung für JLC3DP

1. LadenSie die drei STL Dateien aus dem letzten Punkt herunter.
2. Öffnen sie [https://jlc3dp.com](https://jlc3dp.com) in ihrem Browser.
3. Klicken sie auf "Order Now"
4. Klicken sie auf "Add 3D Files" und fügen sie alle drei STL-Dateien nacheinander hinzu.
5. Für die beiden Gehäuseteile wählen sie einen passenden Druckprozess und Material aus. `FDM(Plastic)` -> `PLA` funktioniert gut, aber andere Materialien sollten auch verwendbar sein.
6. Für den PEP_adapter.stl wählen sie `FDM(Plastic)` -> `TPU`
7. Farben können beliebig gewählt werden.
8. Drücken Sie "Save to cart" und folgen Sie dem Bestellprozess.
9. `Global Direct Line`-Versand ist mit Abstand der Billigste, dauert dafür normal 1-2 Wochen.


Nächster Schritt: [Software installieren](Software_de.md)
