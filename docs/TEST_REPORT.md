# Prüfbericht · Firmware 0.2.0

Dieser Bericht unterscheidet ausgeführte Build-/Hostprüfungen von offenen
Prüfungen mit ESP8266, Originalfernbedienung, IR-Zielgerät und MQTT/HA.
Ein bestandener Hosttest bedeutet keine physische Hardwarefreigabe.

## Ausgeführt

| Prüfung | Ergebnis | Nachweis |
|---|---|---|
| PlatformIO nach jeder Entwicklungsphase 1–10 | SUCCESS | `DEVELOPMENT.md`, lokale Phasenlogs in `artifacts/` |
| Clean des USB-Environment | SUCCESS | `artifacts/clean.log` |
| Vollständiger Clean-Firmware-Build mit `pio run` | SUCCESS | `artifacts/clean-firmware.log` |
| `pio run -e esp_ir_usb -t buildfs` | SUCCESS | `artifacts/littlefs.log`, `littlefs.bin` |
| `pio run -e esp_ir_ota` | SUCCESS | `artifacts/ota-firmware.log` |
| Gemeinsamer Firmware-Core mit nativem GCC | **29/29 Testgruppen bestanden** | `test/results/native.log` |
| Echter Firmware-Codec und IRremoteESP8266 mit UNIT_TEST-Backend | **5/5 Testgruppen bestanden** | `test/results/ir-library.log` |
| Produktions-MQTT-/Discovery-Manager und PubSubClient über echten localhost-TCP-Broker | **11/11 Integrationsgruppen bestanden** | `test/results/mqtt-tcp.log` |
| Lokale JavaScript-Assets und Firmware-MD5 | bestanden | Syntax von `app.js` / `md5.js`; MD5 gegen Node crypto einschließlich Padding-Grenzen |
| Webassets und Dokumentationsbeispiele, statische Prüfung | bestanden | 66 eindeutige HTML-IDs, acht Formulare mit Submit, lokale Assets, Literal-Selektoren, gültige JSON-Beispiele |
| Interaktive Browserprüfung am lokalen Fixture-Server | **9/9 Testgruppen bestanden** | `test/results/web-browser.log`, Screenshots in `artifacts/ui-*.png` |

USB- und OTA-Firmware: **576816 Byte Flash / 1044464 Byte Firmwarebereich**
(55,2 %); **45652 Byte statischer RAM / 81920 Byte** (55,7 %). IR-Puffer,
RAW-Vektoren, Netzwerk und JSON benötigen zusätzlich Laufzeit-Heap. Die Werte
sind keine Messung des verbleibenden Heaps unter realem WLAN-Betrieb.

Festgelegte Buildabhängigkeiten: espressif8266 4.2.1 / Arduino-ESP8266 3.1.2,
IRremoteESP8266 2.8.6, ArduinoJson 6.21.5, PubSubClient 2.8.
Die Compilerlogs enthalten eine Signedness-Warnung in PubSubClient 2.8,
`PubSubClient.cpp:523`; es gibt keine Buildfehler. Die Library wurde dafür nicht
lokal verändert und die Warnung wurde nicht unterdrückt.

## Die 16 angeforderten Prüfungen

| Nr. | Anforderung | Ausgeführte Prüfung | Verbleibende Hardware-/Integrationserprobung |
|---:|---|---|---|
| 1 | Clean PlatformIO build | Tatsächlich bereinigt und vollständig für ESP8266 gebaut; SUCCESS | Flashen und Start auf konkreter Platine |
| 2 | LittleFS build | Tatsächlich `littlefs.bin` aus den lokalen Webdateien erzeugt; SUCCESS | Upload, Mount und HTTP-Auslieferung auf Board |
| 3 | JSON Serialization/Deserialization | Roundtrip für NEC, RAW, 64-Bit-Hexwerte und vollständige Statebytes | Dateisystemzugriffe auf echter Flashbestückung |
| 4 | Leere Konfiguration | Leerer Remote-Index und `{}`-Laufzeitkonfiguration laden gültige Defaults | Erster Start mit frischem LittleFS und Setup-AP |
| 5 | Mehrere Remotes | Mehrere Remotes/Buttons mit eindeutigen IDs und Dateipfaden laden | Nutzung bei laufendem WLAN/MQTT und Heapmessung |
| 6 | Taste löschen | Tatsächlich gemeinsam genutzte Löschfunktion ausführen, restlichen Index erneut laden | HTTP-/MQTT-/HA-Zusammenspiel am Board |
| 7 | Dekodierten NEC-Code lernen | Echter Library-Sender erzeugt NEC, echter Decoder empfängt, Firmware-Codec und Learning bestätigen, JSON speichern/laden | Originalfernbedienung durch demodulierenden Boardempfänger |
| 8 | RAW-Code lernen | Echter UNKNOWN/HASH-Empfang, Firmware-Codec/Learning/JSON erhalten Timings | Unbekannte physische Fernbedienung und Zielgerät |
| 9 | RAW Replay | Tatsächlicher `IRsend::sendRaw`, Timings identisch; NEC erneut dekodiert; Full-State Daikin vollständig erhalten | IR-LED, Carrier, Duty-Cycle und Zielreaktion messen |
| 10 | MQTT reconnect | Produktionsmanager und echter PubSubClient mit TCP-Broker: Ausfall/Wiederkehr, retained LWT, Commands, Lernpause; zusätzlich Retry-/Wraparound-Unitprüfungen | Netzwerk-/Funkbedingungen auf dem tatsächlichen ESP8266 |
| 11 | WiFi reconnect | Dieselbe Zeitsteuerung wie Firmware inklusive Wraparound geprüft | AP-Ausfall, Wiederkehr, 45-s-Setup-AP, DNS, Clientwechsel |
| 12 | HA Discovery generation | Tatsächlicher JSON-Generator und Produktions-Discovery-Manager; retained JSON über TCP, ein Gerät, Stable IDs und Rename; Button/ON-OFF/Opt-in-Toggle als Coretests | Aufnahme der Discovery in echte HA-Instanz |
| 13 | HA Discovery Entity löschen | Produktionsmanager mit Broker-Unterbrechung, persistenter Host-Datei und Offline-Löschung: leere Payload empfangen; neuer Subscriber bekommt keine alte retained Entity mehr | LittleFS auf ESP8266 und Entityentfernung in HA |
| 14 | Sehr langes RAW | 4095 Timings über echten Codec/Library-Replay; lange >65535-µs-Gaps mit Zero-Splits; RX-Overflow abgelehnt | Maximales Signal bei aktivem Netzwerk, Heap und WDT auf ESP8266 |
| 15 | Ungültiges Backup | Versionen, Duplikate, ungültige Daten, abgeschnittenes JSON, Trailing-Data und Passwortfelder abgelehnt; aktive DB erhalten | Multipart-HTTP-Upload, voller Flash und Webauth auf Board |
| 16 | Neustart während/kurz nach Save | Fault-Injection in derselben atomaren Speicherlogik: Unterbrechung beim Schreiben, Prüfen und vor/nach Replace; alte oder neue gültige Datei | Echte Stromunterbrechungen während LittleFS-/Flash-Schreibzugriffen |

Die Host-Fault-Injection benutzt eine `FileOps`-Implementierung, deren Replace
atomar ist. Sie prüft die Transaktionsreihenfolge der Firmware. Sie emuliert
keinen Flashcontroller und keine physische LittleFS-Stromverlustsituation.
Die Scheduler-Unitprüfungen emulieren keine Funkverbindung. Die zusätzlichen
TCP-Integrationstests führen dagegen die unveränderten Produktionsquellen
`src/mqtt_manager.cpp`, `src/command_manager.cpp` und `src/discovery_manager.cpp` zusammen mit PubSubClient
2.8 gegen einen echten lokalen aMQTT-Broker aus. Nur die Hardwaregrenzen werden
adaptiert: WiFiClient verwendet Host-TCP-Sockets, LittleFileOps verwendet Hostdateien,
WiFi/Heap-Telemetrie sind Fixtures, die physische IR-Ausgabe ist ein Spy.
Lernzustände kommen aus derselben LearningSession wie in der Firmware.
Dies prüft die MQTT-Verbindung und die persistente Discovery-Steuerung, jedoch
weder ESP8266-Funk/TCP-Stack noch LittleFS noch eine Home-Assistant-Oberfläche.

Weitere bestandene Coreprüfungen: Fast/Reliable Learn mit Timingtoleranz,
Ablehnung gehaltener Tasten, Timeout und Wraparound, ungültige JSON-Felder,
passwortfreier Konfigurationsexport und Power-Zustandsmodell. Die IR-Library-
Tests decken zusätzlich vollständige Daikin-Statepakete und 150000-µs-Pausen
mit Mark-/Space-Erhalt ab. Das UNIT_TEST-Backend erfasst die vom tatsächlichen
Sendecode erzeugten Mark-/Space-/Carrier-Aufrufe und versorgt den tatsächlichen
Decoder damit. Es misst keine GPIO-Wellenform.

Die abschließenden Regressionen prüfen zusätzlich Reliable Learn über die
65535-µs-Splitgrenze hinweg, RAW-Allokationen mit begrenztem Speicherbudget,
konservative AUTO-Wiedergabe bei abweichender Frequenz, strikt typisierte
Import-/API-Felder und NUL-Zeichen in MQTT-Commands. Ungültige Aktionen dürfen
keinen IR-Sendeaufruf auslösen. Bei reinen Remote-Renames bleiben nicht
mitgesendete Einstellungen erhalten.

## Weboberfläche

JavaScript ist syntaktisch geprüft. Alle Assets liegen lokal in `data/`;
das LittleFS-Image enthält HTML, CSS und JavaScript. Ein lokaler Fixture-Server
wurde für API-Antworten vorbereitet, ist aber kein ESP8266-Webserver.
Eine isolierte Headless-Chromium-Instanz führte neun Testgruppen gegen den
zustandsbehafteten Fixture-Server aus: Startseite, Icons/Favoriten/Reihenfolge,
Richtungstasten, Szenen speichern/senden, zwei Tasten lernen und testen,
Auftragshistorie, Ablehnung falscher Firmware und Upload eines echten Images
mit MD5, Recovery-Einstellungen und 390-px-Mobilansicht. Screenshots wurden
zusätzlich visuell geprüft. Diese Prüfung benutzt die tatsächlichen Webassets,
emuliert aber die HTTP-Antworten. ESP8266-HTTP-Parser, Authentifizierung,
Multipart-/Flashverhalten und der vollständige Importablauf bleiben am Gerät
zu prüfen. Reproduzierbares Skript: `scripts/run_web_tests.py`.

## Zusätzliche Prüfungen für 0.2

Sieben neue Coregruppen prüfen Szenenreferenzen und Index-/Backup-Roundtrips,
Layouts/Favoriten/Reihenfolge, falsche Metadaten, Queue-Limits und Abbruch,
Fehler mit Teilfortschritt, Request-IDs, Delay-Beginn nach abgeschlossener TX,
Millis-Wraparound und lange Inaktivität, fünf HA-Sensoren und Szenen-Discovery,
Recovery-Passwortvalidierung/Geheimnisfreiheit/Fallback-Policy sowie Firmware-
Header/Größe/Targetmarker/abgeschnittene Streams. Drei zusätzliche TCP-Gruppen
prüfen korrelierte queued/sent-Resultate, Szenen und abgelehnte doppelte Request-
IDs sowie die fünf tatsächlich publizierten Diagnosekonfigurationen.

Zusätzlich am Board prüfen: sechs volle Aufträge bei aktivem WLAN/MQTT und
realistischem Heap, verzögerte Szenen und Abbruch zwischen TX, Batch Learn mit
Originalfernbedienung, HA-Sensoraufnahme, Browserupload mit/ohne Authentifizierung,
OTA-Passwort, mehreren Dateiteilen, falschem MD5 sowie Abbruch einschließlich
Abbruch nach dem letzten Datenchunk; anschließend normale Befehle erneut senden.
Den geschützten AP beim ersten Boot, bei WLAN-Ausfall mit aktiviertem/deaktiviertem
Fallback sowie den seriellen Befehl `a` prüfen. Die sichere Nichtaktivierung eines
abgebrochenen Images und der Erhalt von LittleFS wurden hier nicht physisch auf
Flash-Hardware gemessen.

## Praktische Abnahme am Gerät

1. Versorgung, TX/RX-Pads, 3,3-V-UART-Pegel, Senderpolarität und Flashgröße
   prüfen. USB-C/UART-Funktion der tatsächlichen 303ESPIRTR3-Platine bestätigen.
2. USB-Firmware und LittleFS hochladen; Setup-AP öffnen, WLAN/MQTT konfigurieren.
   Firmware-/Flash-/Heapwerte unter `/diagnostics` prüfen.
3. NEC-Taste sowie unbekannte RAW-Taste zuerst schnell, dann zuverlässig lernen.
   Kurz drücken/loslassen; langes Halten darf keine drei Bestätigungen liefern.
4. Beide Tasten senden, Original-/Replay-Signal mit geeignetem Messgerät vergleichen.
   Carrier bei RAW gegebenenfalls manuell anpassen. Echosperre beobachten.
5. Neustarten; dieselben Daten laden und dieselben Zielreaktionen erhalten.
   Nach Relearn muss die alte Taste bei abgebrochenem Save erhalten bleiben.
6. Für Full-State die vollständige Wirkung inklusive Mode/Temperatur/Timer
   prüfen. Für lange Signale Heap und Watchdog bei laufendem MQTT beobachten.
7. Broker ausschalten/einschalten, retained Online/Offline, LWT, Reconnect und
   erneute Discovery prüfen. WLAN trennen, Setup-AP nach 45 s und Rückkehr prüfen.
8. HA-Buttons bedienen, umbenennen, offline löschen, ESP neu starten und Broker
   zurückbringen. Gelöschte Entities müssen verschwinden. Power-Toggle muss ein
   Button bleiben, bis die ausdrücklich optimistische Zusatzoption aktiviert ist.
9. Backup exportieren/importieren, ungültiges Backup sowie fehlgeschlagenen
   Multipart-Upload prüfen. Passwortfelder dürfen im Export nicht vorkommen.
10. OTA mit/ohne eingestelltem Passwort und unterbrochenem Upload prüfen;
    Lerncodes bei reinem Firmwareupdate müssen erhalten bleiben.
11. Mit exportiertem Backup als Rückfall mehrere echte Stromunterbrechungen beim
    Lernen/Umbenennen/Importieren durchführen; anschließend alte oder neue
    gültige Datenbank und keine fehlenden referenzierten Codes erwarten.

Keine dieser praktischen Abnahmeprüfungen wurde hier als bestanden verbucht.
Bis dahin ist Firmware 0.2.0 ein kompilierter und hostgeprüfter Stand mit
ausstehender Hardwareerprobung.

## Prüfungen wiederholen

```console
python scripts/verify_builds.py
python scripts/run_native_tests.py
python scripts/run_mqtt_tests.py
python scripts/check_web_assets.py
python scripts/run_web_tests.py
python scripts/package_release.py
```

Für den nativen Runner wird `g++` benötigt; Einrichtung und die alternative
lokale MinGW-Toolchain sind im [README](../README.md) beschrieben. Die Logs und
generierten Binärdateien sind per `.gitignore` vom Quellprojekt ausgeschlossen.
Für den TCP-Test sind die ausschließlich auf dem Host verwendeten Pakete in
`scripts/mqtt-test-requirements.txt` zu installieren. Das vollständige ZIP enthält
die Quellen, drei Build-Images, neun Prüfprotokolle und ein SHA-256-Manifest.
