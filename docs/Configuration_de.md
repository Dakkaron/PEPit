# Konfiguration

Nachdem das PEPit betriebsbereit ist, muss es noch einmalig konfiguriert werden. Dafür muss das Gerät abgeschaltet und die SD-Karte mit einem Computer verbunden werden.

Auf der SD-Karte befinden sich zwei Dateien, die angepasst werden müssen.

## systemconfig.ini

Hier ist eine Beispiel-systemconfig.ini:

    [system]
    # Wifi credentials used for connecting to trampoline
    # and for getting time and date to log physiotherapy executions
    # Up to 3 wifi access points can be configured
    # WLAN Name und Password fuer die Verbindung zum Trampolinsensor
    # sowie um Zeit und Datum fuer zum Aufzeichnen der Therapie zu empfangen
    # Bis zu drei WLAN-Verbindungen können konfiguriert werden
    wifiSSID=
    wifiPassword=
    wifiSSID2=
    wifiPassword2=
    wifiSSID3=
    wifiPassword3=
    # IP address of the trampoline sensor
    # IP-Adresse des Trampolinsensors
    trampolineIp=
    # Log executions to /executionsLog.txt on the SD card.
    # Zeichne Uebungen auf und schreibe sie nach /executionsLog.txt
    # auf der SD-Karte.
    logExecutions=true
    # Timezone difference from UTC in minutes
    # Zeitzonenverschiebung zu UTC in Minuten
    timezoneOffset=60
    # Simulate PEP exercises
    # Simuliere PEP-Uebungen
    simulateBlowing=false
    # Simulate Trampoline exercises.
    # If no Wifi credentials or Trampoline IP are used,
    # simulateTrampoline will be activated automatically.
    # Simuliere Trampolinuebungen.
    # Wurden keine Wifi-Zugangsdaten oder keine Trampolin-IP
    # eingegeben, wird die Trampolinsimulierung automatisch
    # aktiviert. 
    simulateTrampoline=true

### WLAN-Konfiguration

Um automatisch nach Updates zu suchen und um im Ausführungs-Log Datum und Uhrzeit einzutragen wird WLAN benötigt.

Dazu muss der WLAN-Netzwerkname ohne Leerzeichen direkt nach `wifiSSID=` und das Passwort nach `wifiPassword=` eingefügt werden:

    wifiSSID=WlanName123
    wifiPassword=WlanPasswort123

Es können bis zu drei WLANs konfiguriert werden.

Dabei sind folgende Einschränkungen zu beachten:

- Das WLAN muss 2.4GHz-fähig sein. Die meisten WLANs sind so eingestellt, dass sie ein 2.4GHz-Netz bereit stellen. Sollte keine Verbindung hergestellt werden können, dann sollte man die passende Einstellung in den Router-Einstellungen kontrollieren.
- WLANs mit Username+Passwort werden aktuell nicht unterstützt.
- WLANs, bei denen man im Browser die AGBs akzeptieren muss, werden mangels Browser nicht unterstützt.

Sollte man kein kompatibles WLAN zur Verfügung haben (z.B. im Spital), dann kann man stattdessen einen Handy-Hotspot verwenden. Dabei ist besonders darauf zu achten, dass man in den Hotspot-Einstellungen 2.4GHz auswählt und nicht 5GHz.

### Simulierte Sensoren

    simulateBlowing=false
    simulateTrampoline=true

Für einen Demo-Betrieb (wenn man z.B. die Funktionaität herzeigen oder Spiele entwickeln möchte) kann man Sensorsimulation einschalten.

`simulateBlowing=true` bedeutet, dass der Luftdrucksensor für Exhalation und Inhalation nicht verwendet wird und stattdessen simulierte Sensordaten verwendet werden. Im Normalbetrieb sollte das also auf `simulateBlowing=false` gesetzt werden.

`simulateTrampoline=true` bedeutet, dass statt einem Trampolinsensor simulierte Daten verwendet werden. Da der Trampolinsensor aktuell noch nicht veröffentlicht wurde, soll im Normalbetrieb dieser Wert auf `simulateTrampoline=true` gesetzt werden. In diesem Modus kann ein Trampolin-Spiel als Timer für das Trampolinspringen verwendet werden.

## profiles.ini

Die profiles.ini beinhaltet die Physiotherapie-Profile. Ein Profil beinhaltet einen oder mehrere Tasks, die vorgeben welche Art von Therapie ausgeführt werden soll.

Jedes Profil beginnt mit so einem Profil-Tag:

    [profile_0]

Die Zahl nach `profile_` muss einzigartig und lückenlos fortlaufend sein. Das erste Profil muss `[profile_0]` sein. Es können aktuell bis zu 8 Profile auf einem Gerät konfiguriert werden.

Auf das Profile-Tag folgen die generellen Einstellungen des Profils:

    name=PEP S
    imagePath=/gfx/profile_pep_s.bmp
    cycles=3

Der Wert nach `name=` wird im Hauptmenü angezeigt und im Ausführungs-Log nach vollständiger Ausführung gespeichert.

Der Wert nach `imagePath=` muss mit `/` beginnen und gibt den Pfad zu einem Bild auf der SD-Karte an. Das Bild sollte maximal 32x48 Pixel groß sein und muss im BMP-Format gespeichert werden.

Der Wert nach `cycles=` gibt an wie oft die gesamte folgende Therapieroutine wiederholt wird.

### Tasks

Ein Task gibt eine bestimmte Tätigkeit an, die genau gleich mehrfach ausgeführt werden kann. Ein Profil kann bis zu 10 Tasks beinhalten. Diese müssen wieder beginnend bei 0 lückenlos fortlaufend und einzigartig numeriert werden.

Dabei gibt es vier verschiedene Task-Arten die unterschiedlich konfiguriert werden müssen.

#### PEP Tasks

PEP Tasks sind Tasks bei denen gegen einen Widerstand ausgeatmet werden muss, z.B. mit einem Pari PEP S, einem RC Cornet/RC Cornet+ oder einem PEP/RMT.

    task_0_type=pepLong
    task_0_minStrength=100
    task_0_targetStrength=170
    task_0_repetitions=10
    task_0_time=5000
    task_0_changeText=2.5mm
    task_0_changeImagePath=/gfx/peps_2.5.bmp
    task_1_type=pepShort
    task_1_minStrength=170
    task_1_targetStrength=300
    task_1_repetitions=5
    task_1_time=1000
    task_1_changeText=5mm
    task_1_changeImagePath=/gfx/peps_5.bmp

Hier sehen wir ein Profil, welches aus zwei Tasks besteht. Einem langen Ausatmen mit 2.5mm Stenose für 5 Sekunden bei 17 Millibar welches 10x wiederholt wird, und einem kurzen, festen Ausblasen mit 5mm Stenose für 1 Sekunde bei 30 Millibar, welches 5x verwendet wird.

`task_0_type=` teilt dem Spiel mit, was für eine Art PEP-Task verwendet wird. Besteht das Profil nur aus Tasks mit gleich langem Ausatmen, dann verwendet man hier `task_0_type=pepEqual`. Besteht das Profil hingegen aus abwechselnden Segmenten mit langem und mit kurzem Ausathmen, dann verwendet man für den einen Task `task_0_type=pepLong` und für den anderen Task `task_1_type=pepShort` respektive. (Aus Rückwärtskompatibilitätsgründen sind alternativ auch die Werte `task_0_type=equalBlows`, `task_0_type=longBlows` und `task_0_type=shortBlows` erlaubt. Die sind in ihrer Funktion identisch zu den neueren `pepEqual`, `pepLong` und `pepShort`.)

`task_0_minStrength` gibt an, welcher Druck allermindestens gehalten werden muss. Fällt der Druck darunter gilt die Exhalation als abgebrochen bzw. fehlgeschlagen. Die Einheit hierfür sind Zehntel-Millibar. Für einen Druck von 10 Millibar muss man hier also `task_0_minStrength=100` eingeben.

`task_0_targetStrength` gibt an, welcher Druck idealerweise gehalten werden soll. Wird der Druck erreicht, dann ist die Druckleiste am unteren Rand des Bildschirms voll und manche Spiele belohnen es, wenn dieser Druck erreicht wird. Die Einheit hierfür sind Zehntel-Millibar. Für einen Druck von 17 Millibar muss man hier also `task_0_targetStrength=170` eingeben.

`task_0_repetitions=` gibt an, wie oft ausgeatmet werden muss um den Task fertig zu machen.

`task_0_time=` gibt an, wie viele Millisekunden der Minimaldruck mindestens gehalten werden muss. Für 5 Sekunden muss man also `task_0_time=5000` eintragen.

`task_0_changeText=` gibt an, welcher Text am Bildschirm erscheint, wenn zu diesem Task gewechselt wird. Damit kann man z.B. die Nutzerin/den Nutzer anweisen auf eine andere Stellung des Geräts zu wechseln.

`task_0_changeImagePath=` gibt an, welches Bild für den Wechsel angezeigt wird. Das Bild muss 320x240 Pixel groß sein und als BMP abgespeichert werden. Der angegebene Pfad muss mit `/` beginnen und auf eine Datei auf der SD-Karte zeigen.

#### Inhalation Tasks

Inhalations-Tasks werden in Kombination mit Inhalationsgeräten wie dem Pari Boy + LC-Sprint oder dem Pari eFlow Rapid verwendet. Um den PEPit mit diesen Inhalationsgeräten zu verbinden wird das Mittelteil des PEP S auf das kleinste Loch gestellt und zwischen das Reservoir und das Mundstück des Inhalationsgeräts gesteckt. Dabei gibt der PEP S-Mittelteil keinen Widerstand sondern wird nur als Adapter für den Schlauch verwendet.

Inhalationstasks sollten der einzige Task in einem Profil sein und `cycles=` sollte auf `cycles=1` gesetzt sein.

  task_0_type=inhalation
  task_0_minRepetitions=20
  task_0_time=2500
  task_0_minStrength=15
  task_0_targetStrength=30

`task_0_minRepetitions=` gibt an, wie viele Atemzüge mindestens gemacht werden müssen, damit der "Fertig"-Knopf eingeblendet wird und der Task beendet werden kann.

Die anderen Werte sind identisch in ihrer Bedeutung zu den Werten aus den PEP Tasks

#### Kombinierte Inhalation + PEP Tasks (verfügbar ab Version 5.0)

Der Pari PEP S kann in seiner Gesamtheit auf einen LC-Sprint oder eFlow Rapid aufgesteckt werden. Dabei wird nicht nur das Mittelstück verwendet. Stattdessen ersetzt der PEP S das reguläre Mundstück des Inhalationsgerätes. Auf einen Inhalations-Zug wird dann eine Ausatmung gegen den Widerstand des PEP S verwendet. Wie bei der Inhalation sollte ein `inhalationPep` Task der einzige Task in einem Profil sein und `cycles=1` sollte gesetzt sein.

    task_0_type=inhalationPep
    task_0_minRepetitions=10
    task_0_inhalationTime=2500
    task_0_inhalationMinStrength=15
    task_0_inhalationTargetStrength=30
    task_0_exhalationMinStrength=100
    task_0_exhalationTargetStrength=170
    task_0_exhalationTime=5000

`task_0_minRepetitions=`, `task_0_inhalationTime=`, `task_0_inhalationMinStrength=` und `task_0_inhalationTargetStrength=` funktionieren wie bei dem Inhalationstask.

`task_0_exhalationMinStrength=`, `task_0_exhalationTargetStrength=` und `task_0_exhalationTime=` funktionieren wie bei PEP Tasks.

#### Trampolin Task

Der Trampolinsensor ist aktuell noch nicht veröffentlicht und befindet sich noch in Arbeit. Der Sensor ist ein eigenständiges Gerät, welches sich per Bluetooth mit dem PEPit verbindet. Er erkennt wie oft und wie hoch die Spielerin/der Spieler am Trampolin springt. Da dieser Sensor allerdings noch nicht verfügbar ist, wird aktuell das Trampolin simuliert. In diesem Modus funktioniert das PEP als ein Timer für das Trampolinspringen.

    task_0_type=trampoline
    task_0_time=300000

`task_0_time=` gibt an, wie viele Millisekunden gehüpft werden muss. Für 5 Minuten ist das also `task_0_time=300000`.

## Spielstände zurücksetzen

Wird das PEPit (z.B. in einem Klinik-Setting) an einen anderen Nutzer weitergegeben, kann es sinnvoll sein die gespeicherten Spielstände zurückzusetzen.

Dafür muss im Hauptverzeichnis der SD-Karte eine leere Textdatei mit dem Namen `resetSaves` erstellt werden. Die SD-Karte wird dann wieder in das PEPit gesteckt und das PEPit wird gestartet.

Das PEPit löscht dann die Spielstände und die Datei und kann an den nächsten Nutzer weiter gegeben werden.
