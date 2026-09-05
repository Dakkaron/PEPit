## Luftdruck-Sensor-Datenfilterung

Die Rohdaten des Luftdrucksensors haben ein paar Probleme, die durch Software behandelt werden.

# Der atmosphärische Umgebungsdruck variiert

Abhängig von Höhe, Temperatur und Wetter variiert der Luftdruck rund um das Gerät. Um dafür zu korrigieren tariert sich das PEPit direkt nach dem Start auf den Umgebungsdruck. Das passiert innerhalb der ersten Sekunde nach dem Einschalten. Wenn das Logo oder das Hauptmenü erscheint ist die Tarierung schon abgeschlossen.

Damit die Tarierung korrekt funktioniert muss der Luftweg zum Luftdrucksensor den gleichen Luftdruck haben wie die Umgebungsluft. Dafür lässt man beim Einschalten des Gerätes am Besten den Luftschlauch abgesteckt. Alternativ muss man darauf achten, dass der Luftschlauch knickfrei ist und nicht zugehalten wird. Das Inhalations- oder Therapiegerät darf sich nicht im Mund befinden.

Sollte die Tarierung falsch ausgeführt werden, kann es passieren dass das Gerät sich auf den falschen Wert tariert und es dann insbesondere am Anfang der Übungen falsche Werte misst.

# Der gemessene Druck hat falsche Spikes

Durch das Inhalieren/Exhalieren ändert sich nicht nur der Druck im Luftschlauch, sondern auch die Geschwindigkeit der Luft im Schlauch. Wird der Atemzug plötzlich unterbrochen, dann "schlägt" die Luft im Luftschlauch plötzlich zurück. Das führt zu sehr kurzen aber sehr starken Ausschlägen am gemessenen Luftdruck, oft über 500mBar.

Um diese Messwerte zu korrigieren werden zwei Mechanismen eingesetzt.

- Werte über +-200mBar werden ignoriert. Diese Grenze liegt weit über allen Werten die normal in der Nutzung vorkommen sollten, können also als Sensor-Artefakte ignoriert werden.
- Es wird ein Rolling Average Filter verwendet um den Sensor-Output zu glätten. Dazu werden die letzten gemessenen Werte gesammelt und ein Durchschnitt dieser letzten Werte als tatsächlicher Output zurückgeliefert.

# Der Sensorwert kann driften

Bei starken Temperaturänderungen (z.B. durch Lüften) oder wenn die Batteriespannung unter 3.6V fällt, kann es dazu kommen dass der Sensorwert driftet. Das ist eine langsame graduelle Änderung aller gemessenen Werte. So kann der gemessene Wert z.B. langsam um ein paar mBar nach oben oder unten wandern.

Um das zu korrigieren tariert die Software automatisch nach. Das passiert jedes Frame, also normalerweise zwischen 15-80 Mal pro Sekunde.

Da der Sensordrift langsam und graduell passiert, ist auch die Autotarierung graduell. Bei jeder Messung wird die Tarierung leicht angepasst.

Ein Threshold-Filter sorgt dabei dafür, dass der Ruhezustand erkannt wird und die Tarierung in diesem Zustand passiert.

Um Fehler bei der initialen Tarierung während dem Hochfahren des Geräts auszugleichen, wird auch bei höheren Drücken autotariert, dort aber sehr langsam um keine Auswirkung auf tatsächliche Atemzüge zu haben.
