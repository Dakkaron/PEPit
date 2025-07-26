# SD-Karte vorbereiten

1. Die SD-Karte mit einem Computer verbinden. Dafür kann entweder ein integrierter SD-Karten-Slot im PC oder ein SD-Karten-Adapter verwendet werden.
2. Die Datei sdcardcontent.zip vom neuesten [Release](https://github.com/Dakkaron/T-HMI-PEPmonitor/releases) herunterladen und den Inhalt auf die SD-Karte entpacken.

# Firmware auf LILLYGO T-HMI aufspielen

1. Das neuesten firmware.bin, bootloader.bin und partitions.bin von der [Release-Seite](https://github.com/Dakkaron/T-HMI-PEPmonitor/releases) herunterladen.
2. LILLYGO T-HMI per USB-Kabel mit dem PC verbinden. Sollte das Display des LILLYGO T-HMI nicht angehen, sobald es mit dem PC verbunden ist, bitte ein USB-A-auf-USB-C-Kabel verwenden. Bei der Verwendung von USB-C-auf-USB-C-Kabeln kann es zu Problemen kommen.
3. [flash_download_tool](https://dl.espressif.com/public/flash_download_tool.zip) herunterladen und ausführen.

4. Im flash_download_tool `ChipType:` auf `ESP32-S3` stellen und mit `OK` bestätigen.

![flash_download_tool](https://raw.githubusercontent.com/Dakkaron/T-HMI-PEPmonitor/refs/heads/main/docs/images/flashdownloadtool1.png)

5. Auf der zweiten Seite folgende Einstellungen auswählen:
   * Oberste Checkbox am linken Rand aktivieren
   * Im Textfeld rechts neben der Checkbox den Pfad zu `bootloader.bin` angeben
   * In das Feld rechts daneben `0` eingeben.
   * Das Gleiche mit der nächsten Zeile wiederholen: Dort muss jetzt `firmware.bin` und `0x10000` eingegeben werden.
   * In der nächsten Zeile muss `partitions.bin` und `0x8000` stehen.
   * Rechts unten bei `BAUD:` 921600 eingeben.
   * Bestätigen mit einem Click auf `START`

![flash_download_tool](https://raw.githubusercontent.com/Dakkaron/T-HMI-PEPmonitor/refs/heads/main/docs/images/flashdownloadtool2.png)

6. Wenn das Hochladen abgeschlossen ist, kann der T-HMI vom PC getrennt werden.

# Testen

1. MicroSD-Karte in den Slot im T-HMI einschieben.
2. Das T-HMI per USB mit dem PC verbinden.
3. Das T-HMI sollte beim ersten Start die Touchscreen-Kalibrierung anzeigen, danach wird das Hauptmenü angezeigt.

Nächster Schritt: [Zusammenbau](Assembly_de.md)
