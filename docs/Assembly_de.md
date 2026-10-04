# Löten

- Das bei dem T-HMI mitgelieferte Grove-Kabel (das Kabel mit den vier Adern) in der Mitte durchschneiden und die Adern rund 5mm tief abisolieren.
- Die vier Adern in der folgenden Reihenfolge an den Luftdrucksensor anlöten:
   - Schwarz -> GND
   - Rot -> VCC
   - Gelb -> OUT
   - Weiß -> SCK

# Joystick-Kabel vorbereiten

- Der M5stack Joystick2 hat keine Power-Saving-Features. Deswegen muss das Grove-Kabel umgebaut werden, damit das T-HMI den Joystick abschalten kann.
- Dazu muss der rote Draht aus dem Grove-Connector entfernt werden und auf die zweite Position von rechts auf einem ansonsten leeren Grove-Connector verbunden werden.

# Verkablen

- Den Grove-Connector vom Luftdrucksensor in den mittleren Grove-Steckplatz am T-HMI anschließen.
- Den Grove-Connector vom M5stack Joystick2, der noch drei Drähte hat, mit dem linken Grove-Steckplatz (IO15 und IO16) verbinden.
- Den Grove-Connector vom M5stack Joystick2, der einen Draht hat, mit dem rechten Grove-Steckplatz (IO43) verbinden.
- Den Akku an den `BAT`-Steckplatz am T-HMI anschließen

# Verschrauben

- Das T-HMI in den oberen Teil des Gehäuses einsetzen und mit den vier M2x10-Schrauben verschrauben.
- Die vier Muttern in die Aussparungen des unteren Gehäuses eindrücken.
- Die Batterie mittels Patafix oder doppelseitigem Klebeband (z.B. Tesa Power Strips Small) in die untere Gehäusehälfte kleben.
- Den Luftdrucksensor in die passende Aussparung in der oberen Gehäusehälfte einsetzen.
- Den M5stack Joystick2 in die passende Aussparung in der oberen Gehäusehälfte einsetzen. Dazu muss der Kopf des Joysticks zuerst abgenommen und nach dem Einsetzen wieder aufgesteckt werden.
- Die obere Gehäusehälfte auf die Untere aufsetzen.
- Die vier M3-Schrauben in die Ecken der oberen Gehäusehälfte einsetzen und festschrauben.

Nächster Schritt: [Konfiguration](Configuration_de.md)
