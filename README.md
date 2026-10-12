# ESP IR Bridge · 0.2.0

PlatformIO-Firmware für **ESP_IR_TR_WIFI / 303ESPIRTR3 / ESP-12F (ESP8266)**.
Originalfernbedienungen lernen, in LittleFS speichern und über Weboberfläche,
MQTT oder automatisch angelegte Home-Assistant-Buttons wiedergeben. RAW-Daten
werden bei jedem gültigen Empfang gespeichert, auch bei erkanntem Protokoll.

Die sieben Erweiterungen von 0.2 (Szenen, Sendewarteschlange, Tastenfolge,
Layouts, HA-Diagnose, Browserupdates und geschütztes Recovery-WLAN) sind in
[docs/FEATURES_0.2.md](docs/FEATURES_0.2.md) beschrieben.

Der ursprüngliche Quellcode wurde in zehn aufeinanderfolgenden Phasen gebaut. Der Prüfstand
und die Grenzen der Verifikation stehen in [docs/TEST_REPORT.md](docs/TEST_REPORT.md),
die einzelnen Phasen in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
Physische IR-, WLAN-, Broker-, OTA- und Stromunterbrechungstests stehen noch aus.
Die MQTT-/Discovery-Software wurde zusätzlich über echte TCP-Verbindungen an
einem lokalen Broker geprüft; ESP8266-Funk und Home Assistant benötigen weiterhin
die Abnahme am Gerät.

Das vollständige Paket `artifacts/esp-ir-bridge-0.2.0.zip` enthält Projektquellen,
USB-/OTA-Firmware und LittleFS-Image unter `images/`, Prüfprotokolle unter
`checks/` sowie ein SHA-256-Manifest. Die Images verwenden das unten beschriebene
4-MB-Flashlayout. `littlefs.bin` ist für die Erstinstallation gedacht und ersetzt
bei einem Upload vorhandene Laufzeitdaten. Nur Quellen enthält das separate
`esp-ir-bridge-0.2.0-source.zip`.

## Hardware und zentrale Einstellungen

| Funktion | ESP8266-GPIO | Einstellung in `include/config.h` |
|---|---:|---|
| IR-Sender / Treibertransistor | 4 | `IR_TX_PIN` |
| IR-Empfänger | 14 | `IR_RX_PIN` |
| Optionale Status-LED | 2 | `LED_PIN`, `USE_LED` |

Die LED ist zunächst deaktiviert, da Bestückung und Polarität der konkreten
Platine nicht verifiziert wurden. GPIO2 gehört außerdem zu den Boot-Strapping-Pins.
Version, Pins, RX-Puffer, RAW-Limits, Timeout und Wiederholungsabstand stehen zentral
in [include/config.h](include/config.h). Bei invertiertem Sendertreiber kann
`IR_TX_INVERTED` angepasst werden.

Das Buildziel `esp12e` verwendet denselben ESP8266 und hier ein **4-MB-Flashlayout
mit 1 MB LittleFS**, 80 MHz und Flashmodus DOUT. Vor einem Upload die tatsächlich
bestückte Flashgröße prüfen. Nach dem Start zeigt `/diagnostics` erkannte und
konfigurierte Flashgröße. Falls die Platine weniger als 4 MB besitzt, müssen
Board-/Linkerskript- und Flash-Einstellungen vor dem Flashen angepasst werden.
Das PlatformIO-Boardprofil ist in der [offiziellen Boarddokumentation](https://docs.platformio.org/en/latest/boards/espressif8266/esp12e.html)
beschrieben; es bestätigt keine bestimmte Flashbestückung dieser Fremdplatine.

### USB-C: Stand der Überprüfung

Ein Projekt mit genau der Rückseitenmarkierung **ESP_IR_TR_WIFI (303ESPIRTR3)**
dokumentiert das Board und seine Anschlüsse, nennt aber keinen USB-UART-Chip:
[Samurai1201/ESP8266 IR Transceiver](https://github.com/Samurai1201/Esp8266-IR-Transceiver).
Ein Besitzer beschreibt bei einem getesteten **ESP_IR_TR_WIFI** USB-C als reine
Stromversorgung und verwendet einen externen USB-Seriell-Adapter:
[RobThree, Hardware / ESP8266 IR board](https://github.com/RobThree/SAC9010QC-SAC12010QC#esp8266-ir-board).
Diese zweite Quelle ordnet ihre Aussage nicht ausdrücklich der Markierung
303ESPIRTR3 zu. Deshalb ist die USB-C-Datenfunktion der konkreten Revision hier
**nicht abschließend bestätigt**.

Vor Ort mit einem nachweislich datenfähigen USB-C-Kabel auf einen neuen COM-Port
prüfen und nach einem USB-UART-Chip wie CH340, CP210x oder CH9102 suchen.
Kein COM-Port allein beweist keine bestimmte Schaltung; Kabel und Treiber können
ebenfalls die Ursache sein. Die folgende Anleitung verwendet einen externen
3,3-V-USB-TTL-Adapter und benötigt keine angenommene USB-C-Datenfunktion.

### USB-TTL anschließen

**UART-Logik ausschließlich 3,3 V.** Keine 5-V-TTL- oder RS-232-Signale an
ESP8266-GPIOs anschließen. Diese Pegel und die gekreuzte Verkabelung entsprechen
der [Espressif-Anleitung zur seriellen Verbindung](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/serial-connection.html).

| USB-TTL-Adapter | ESP_IR_TR_WIFI-Pad |
|---|---|
| TX, 3,3-V-Logik | RX |
| RX, 3,3-V-Logik | TX |
| GND | GND |
| VCC | Beim hier beschriebenen USB-C-Versorgungsweg nicht verbinden |

Das Board über seinen vorgesehenen USB-C-Stromanschluss versorgen und Adapter-
und Boardmasse verbinden. Die Spannungsanforderung eines zusätzlich vorhandenen
Versorgungspads ist ohne Schaltplan unbekannt. Ein Adapterjumper für VCC ist
kein Nachweis für den tatsächlichen TX-Logikpegel; diesen vorher prüfen.

### Flashmodus, Build und Erstinstallation

PlatformIO Core oder die PlatformIO-Erweiterung für VS Code installieren.
Im Projektverzeichnis:

```console
pio run
pio run -e esp_ir_usb -t buildfs
```

Die festgelegten Abhängigkeiten sind ESP8266-Plattform 4.2.1,
IRremoteESP8266 2.8.6, ArduinoJson 6.21.5 und PubSubClient 2.8. Beim ersten
Build lädt PlatformIO Toolchain, Framework und Libraries herunter.

1. Board stromlos machen. **IO0 / GPIO0 mit GND verbinden**.
2. Strom einschalten oder Reset auslösen, während GPIO0 auf GND liegt.
3. Mit dem tatsächlichen Port flashen, zum Beispiel unter Windows:

   ```console
   pio run -e esp_ir_usb -t upload --upload-port COM5
   ```

4. Erneut in den Bootloader starten und die lokalen Webdateien hochladen:

   ```console
   pio run -e esp_ir_usb -t uploadfs --upload-port COM5
   ```

5. GPIO0-GND-Verbindung entfernen und normal neu starten.
6. Optional den seriellen Monitor öffnen:

   ```console
   pio device monitor --port COM5 --baud 115200
   ```

GPIO0 wird beim Reset ausgewertet. Ohne angeschlossene automatische
Reset-Schaltung kann nach jedem Upload ein manueller Neustart erforderlich sein.
GPIO2 und GPIO15 nicht durch zusätzliche Verdrahtung auf falsche Bootpegel ziehen.
Siehe [Espressif Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).
ROM-Bootmeldungen haben 74880 Baud, Firmware-Logs 115200 Baud.

**`uploadfs` ersetzt das gesamte LittleFS einschließlich angelernter Codes und
Laufzeitkonfiguration.** Für die Erstinstallation ist das vorgesehen. Bei einem
späteren Update zuerst ein Backup exportieren; nach `uploadfs` WLAN erneut
einrichten und das Backup importieren. Ein reiner Firmware-Upload erhält LittleFS,
solange das Flashlayout gleich bleibt. Die Firmware formatiert bei einem
Mountfehler nicht automatisch; die ursprünglichen Daten bleiben zur Diagnose
erhalten. Das Verhalten des Dateisystems ist in der
[ESP8266-LittleFS-Dokumentation](https://arduino-esp8266.readthedocs.io/en/3.1.2/filesystem.html)
beschrieben.

## Erstes Booten und WLAN

Ohne gespeicherte WLAN-Konfiguration erscheint das passwortgeschützte Konfigurationsnetz
**`IRBridge-ABC123`**. Die sechs Zeichen stammen aus der MAC-Adresse.
Mit dem Standardpasswort **`05102001`** verbinden und
**http://192.168.4.1/settings** öffnen. SSID, WLAN-Passwort,
Hostname und optional MQTT-Broker/Zugangsdaten eintragen. Speichern startet neu.
Danach das Gerät über seine Router-IP oder den konfigurierten Hostnamen öffnen.
Der automatisch erzeugte Hostname lautet `irbridge-abc123`.

Die Bridge versucht eine Verbindung alle 15 Sekunden. Ist WLAN 45 Sekunden lang
nicht erreichbar und der automatische Fallback aktiviert, bietet sie zusätzlich
den geschützten Konfigurations-AP an und versucht
weiter die Station-Verbindung. Nach erfolgreicher Verbindung wird der AP beendet.
Dies funktioniert auch nach Neustarts. Für MQTT beträgt das Retry-Intervall
5 Sekunden; neue MQTT-Verbindungsversuche pausieren während einer Lernsession.

Router-/Broker-Zugangsdaten und ein optionales eigenes Recovery-Passwort werden
zur Laufzeit unter `/data/config.json` auf dem Gerät gespeichert. Das Standardpasswort
des Recovery-WLANs ist fest auf `05102001` gesetzt; zufällige Schlüssel werden nicht erzeugt.
Web/API können gemeinsam mit Benutzername und Passwort geschützt werden;
ArduinoOTA hat ein eigenes optionales Passwort. HTTP-Basic-Auth und das hier
verwendete MQTT-TCP übertragen über das lokale Netzwerk ohne TLS. Das geschützte
Setupnetz ist für die lokale Einrichtung und den optionalen Verbindungsausfall-
Fallback vorgesehen. Der Schlüssel kann in Einstellungen geändert werden;
`a` im seriellen Monitor aktiviert das Recovery-WLAN manuell.

## Eine Taste lernen und wiedergeben

1. Auf der Hauptseite eine Remote wie **Ventilator** anlegen.
2. **+ Taste lernen** wählen, Remote und Namen wie **Power** eingeben.
3. Fast Learn oder Reliable Learn mit zwei/drei Aufnahmen wählen und starten.
4. Originalfernbedienung auf den Empfänger richten, Taste kurz drücken und
   loslassen. Für Reliable Learn zwischen Tastendrücken mindestens **400 ms
   Empfangspause** lassen.
5. Nach Status `saved` ist der Code persistent. Die neue Taste sendet beim Anklicken.

Die Zustände sind `idle`, `waiting`, `receiving`, `saved`, `timeout`, `error`.
Eine Session endet nach 30 Sekunden. Reliable Learn vergleicht Protokoll,
Wert/Adresse/Command oder vollständige Zustandsbytes, Repeat-Kennzeichnung,
RAW-Länge und einzelne Timings. Einstellbare relative Toleranz: 15–25 %, Standard
20 %; für kurze Timings gilt zusätzlich ein Mindestspielraum von 80 µs.
Ein gehaltenes Signal und seine Repeatframes zählen nicht als mehrere getrennte
Tastendrücke. Bei abweichenden Aufnahmen beginnt die Kandidatenzählung neu.
Bei langen Timings wird die Gesamtdauer verglichen, sodass eine kleine Abweichung
über die 65535-µs-Splitgrenze die Bestätigung nicht verhindert.

Im Tastenmenü stehen **Send, Rename, Relearn, Delete, View details** zur Verfügung.
Umbenennen und erneutes Lernen erhalten die Button-ID und damit die HA-Entity-ID.
Details zeigen Protokoll, Bits, Adresse, Command, RAW-Länge, Frequenz, Zustandsbytes
und letzten Sendezeitpunkt. Der Sendezeitpunkt liegt im RAM und beginnt nach einem
Neustart erneut bei null, um Flash-Schreibzugriffe pro Tastendruck zu vermeiden.

Auf **`/monitor`** erscheinen Empfangsereignisse live per lokalem Polling.
**Use this signal** speichert genau die angezeigte Sequenz als neue Taste.
Ist inzwischen ein neuer Frame angekommen, muss die Anzeige aktualisiert werden;
es wird nicht stillschweigend ein anderes Signal gespeichert. **RAW Replay** spielt
den letzten gültigen Empfang ab. Im seriellen Monitor gibt `d` das vollständige
RAW-Array aus; `r` spielt den letzten Empfang außerhalb des Lernmodus ab.

### RAW, Protokolle, Trägerfrequenz und Full-State

Alle Standarddecoder der eingebundenen IRremoteESP8266-Version sind aktiviert;
die Firmware setzt NEC nicht voraus. Bekannte Protokolle speichern Dekodierung
**und RAW**, unbekannte Protokolle werden als `RAW` gespeichert. Für Klimaanlagen
und andere Stateful-Protokolle werden die vollständigen Zustandsbytes zusätzlich
gespeichert. Ein solcher Code reproduziert den **gesamten gelernten Zustand**,
einschließlich weiterer Einstellungen im Paket; er verändert nicht ausschließlich
die namensgebende Funktion.

RAW enthält Mark-/Space-Zeiten in Mikrosekunden, beginnend mit dem ersten Mark;
die vorherige Leerlaufzeit gehört nicht zum Code. Lange Abschnitte über 65535 µs
werden in mehrere Abschnitte mit Null-Abschnitten dazwischen aufgeteilt, damit
die Mark-/Space-Parität erhalten bleibt. Überläufe und unvollständige Aufnahmen
werden abgelehnt statt als abgeschnittenes Signal gespeichert.

Ein demodulierender IR-Empfänger erfasst die Hüllkurve, **nicht die tatsächliche
Trägerfrequenz oder das Tastverhältnis**. Das ist eine Hardwaregrenze, die auch
der [IRremoteESP8266-Maintainer erläutert](https://github.com/crankyoldgit/IRremoteESP8266/discussions/1781).
Unbekannte Codes verwenden 38000 Hz. Bei bekannten Protokollen werden verfügbare
Protokollstandards berücksichtigt, zum Beispiel Sony 40000 Hz, RC5/RC6 36000 Hz,
DISH 57600 Hz und Panasonic 36700 Hz. `frequency_source` bezeichnet `default`,
`protocol_default` oder eine manuelle Einstellung; die UI behauptet keine Messung.
Eine abweichende bekannte Frequenz kann in den Tastendetails manuell eingetragen
werden. Die Library verwendet bei RAW ihren Standard für das Tastverhältnis.

| Send mode | Verhalten |
|---|---|
| `AUTO` | Dekodiert nur nach konservativer Prüfung eines einzelnen kanonischen NEC-32-Frames; sonst RAW. Bei nicht unterstütztem Decoder-Sender RAW-Fallback. |
| `RAW` | Die vollständigen gespeicherten Timings mit gespeicherter Frequenz senden. |
| `DECODED` | Generischen protokollspezifischen Sender der Library verwenden, einschließlich State-Byte-Überladung; ein nicht sendbares Protokoll ergibt einen Fehler. |

Diese konservative AUTO-Wahl vermeidet den Verlust zusätzlicher Frameabschnitte
oder vollständiger Zustandsdaten. `repeat_count` ist die Zahl vollständiger
Sendebursts, Standard 1, einstellbar 1–10. RAW wiederholt das gesamte gespeicherte
Paket mit 40 ms Abstand; Repeatframes innerhalb des erfassten Pakets bleiben dabei
erhalten. Der dekodierte Sender kann protokollbedingt Mindestwiederholungen
erzwingen; bei NEC sind zusätzliche Wiederholungen die libraryüblichen Repeatframes.
Während TX ist RX deaktiviert und bleibt anschließend 350 ms gesperrt, bevor es
automatisch wieder aktiv wird.

Die gespeicherten RAW-Daten überstehen die JSON-Serialisierung unverändert.
Ob das Zielgerät die physische Wiedergabe akzeptiert, hängt außerdem von Empfänger,
Trägerfrequenz, Sendertreiber, Ausrichtung und Protokolleigenheiten ab und muss
am eigenen Gerät geprüft werden. Rolling-Codes oder Fernbedienungen mit wechselnden
Toggle-Bits können wiederholte identische Aufnahmen ablehnen; RAW allein umgeht
solche Protokollregeln nicht.

### Ressourcenlimits

Der ESP8266 besitzt begrenzten Heap. Daher gibt es einstellbare Grenzen in
`include/config.h` statt einer Zusage unbegrenzt vieler Codes:

- 16 Remotes und insgesamt 64 Buttons; Name bis 48 UTF-8-Bytes.
- RX-Puffer 4096 Einträge, Abschluss nach 90 ms Empfangspause.
- Bis 8192 gespeicherte RAW-Einträge einschließlich langer Timing-Splits,
  höchstens 2 Sekunden pro Paket; Carrier 10000–60000 Hz.
- Backup bis 900000 Bytes, einzelne Code-Datei bis 60000 Bytes.
- Persistente HA-Discovery-Historie bis 192 Topics.

Frames mit längeren Pausen als der Empfangstimeout werden getrennt. Für eine
besondere Fernbedienung kann `RX_TIMEOUT_MS` innerhalb der Librarygrenzen angepasst
werden. Bei zu wenig zusammenhängendem Heap erfolgt ein Fehler; sehr lange Signale
sollten mit Fast Learn statt mehreren gleichzeitig gehaltenen RAW-Kopien gelernt
werden. Die physische maximale Paketgröße unter aktiven Netzwerkverbindungen ist
noch nicht auf dem Board gemessen.

## MQTT und Home Assistant

In den Einstellungen Broker, Port, Benutzer, Passwort und Discovery-Prefix setzen.
Standardprefix ist `homeassistant`. Die eindeutige Gerätekennung ist die komplette
MAC-Adresse ohne Trennzeichen, etwa `aabbccddeeff`; der AP verwendet ihre letzten
sechs Zeichen. Die Firmwareversion wird in Webstatus, MQTT-Info und HA-Geräteinfo
angezeigt.

| Topic unter `irbridge/<device_id>/` | Zweck |
|---|---|
| `status` | Retained `online`; retained Last Will `offline`, Will-QoS 1 |
| `send` | JSON-Befehl für eine gespeicherte Taste oder Szene, optional `request_id` |
| `learn` | Lernen starten, abbrechen oder Status anfordern |
| `command` | JSON mit `action: "send"`, `action: "macro"` oder `action: "learn"` |
| `command/result` | Nicht-retained Auftrag mit `request_id`, `queued` / `sent` / `failed`, Fortschritt und Fehler |
| `received` | Nicht-retained Empfangsmetadaten; keine großen RAW-Arrays |
| `learn/status` | Nicht-retained Lernstatus und Kandidatenzahl |
| `info` | Retained Version, IP, RSSI, Heap und Uptime |

Beispiel auf `irbridge/aabbccddeeff/send`:

```json
{"remote":"Ventilator","button":"Power"}
```

Remote-/Button-Namen oder stabile IDs sind erlaubt. Befehle ohne Retain senden,
damit ein Power-Toggle nicht bei erneuter MQTT-Verbindung nochmals ausgeführt wird.
Die Firmware verwaltet die Verbindung mit PubSubClient; sie meldet Annahme,
Fortschritt und Abschluss an `command/result`, ohne Bestätigung des IR-Zielgeräts.
Szenen verwenden `macro` anstelle von `button`. Beispiele und Grenzen stehen
in [FEATURES_0.2](docs/FEATURES_0.2.md).

Lernen über `irbridge/aabbccddeeff/learn`:

```json
{"action":"start","remote":"Ventilator","name":"Power","mode":"reliable","confirmations":3,"tolerance_percent":20}
```

Abbruch mit `{"action":"cancel"}`, Status mit `{"action":"status"}`.
Empfangs-RAW ist über die Code-Detail-API oder das Backup zugänglich, damit lange
Arrays nicht den auf 2048 Bytes begrenzten MQTT-Paketpuffer überschreiten.

### Discovery und Power

Jede gelernte Taste erscheint als MQTT-Button, beispielsweise **Ventilator Power**.
Alle gehören zu einem Gerät **ESP IR Bridge** mit Hersteller **Custom**,
Modell **ESP_IR_TR_WIFI / ESP-12F**, Firmwareversion und MAC-basierter Kennung.
Discovery-Topic:

```text
homeassistant/button/irbridge_aabbccddeeff/<button_id>/config
```

Die Payload enthält Name, `unique_id`, `device`, `command_topic`,
`payload_press`, `availability_topic` und Online-/Offline-Payloads gemäß
[Home Assistant MQTT Button](https://www.home-assistant.io/integrations/button.mqtt/).
Zufällige stabile IDs vermeiden Kollisionen durch Namen und Sonderzeichen.
Umbenennen aktualisiert dasselbe Discovery-Topic. Löschen publiziert eine leere
retained Payload. Bereits verwendete Topics bleiben als Löschhistorie in
`/data/discovery.json` und werden nach Reconnect erneut abgeglichen. Damit bleiben
auch Löschungen bei offline stehendem Broker und Neustart bekannt. PubSubClient
publiziert QoS 0; die Historie wird deshalb nicht anhand eines bloßen erfolgreichen
TCP-Schreibens verworfen. Bei 192 historischen Topics wird ein Diagnosefehler
gemeldet; eine Bereinigung dieser Historie erfordert vorherige Broker-Prüfung.

**Power-Toggle ist standardmäßig ausschließlich ein Button.** Die Bridge kennt
keinen tatsächlichen Zustand des Ventilators. Entity Type `Button`, `Toggle` und
`Custom` bleiben Buttons. Mit genau einer als `Switch ON` und einer als
`Switch OFF` markierten Taste erzeugt die Firmware zusätzlich einen MQTT-Switch,
der die getrennten Befehle verwendet. Auch dieser Switch besitzt mangels Rückkanal
keinen gemessenen `state_topic` und arbeitet im optimistischen HA-Modus.

Für genau eine `Toggle`-Taste kann in den Remote-Einstellungen zusätzlich
**Optimistic – real device state unknown** aktiviert werden. Erst dann entsteht
ein zusätzlicher Switch mit ausdrücklich entsprechendem Namen. Andere
Fernbedienungen, verpasste IR-Pakete oder ein Neustart können dessen angenommenen
Zustand verändern. Die HA-Switch-Einstellungen folgen der
[offiziellen MQTT-Switch-Dokumentation](https://www.home-assistant.io/integrations/switch.mqtt/).

## REST-API

JSON-Schreibzugriffe erwarten `Content-Type: application/json`. Optionales
HTTP-Basic-Auth schützt dieselben Endpunkte wie die Weboberfläche. Eine erfolgreiche
Mutation antwortet mit `{"success":true}`. Sendebefehle antworten mit HTTP 202
und einem Auftrag (`request_id`, `status: "queued"`); dessen tatsächlicher
Abschluss ist über `/api/commands` sichtbar. Ein Fehler antwortet mit
`{"success":false,"error":"..."}` und einem passenden HTTP-Fehlercode.
Falsch typisierte Parameter und NUL-Zeichen in Textfeldern werden abgelehnt.
Bei PATCH bleiben weggelassene Eigenschaften erhalten; eine explizite
Trägerfrequenz muss im unterstützten Bereich liegen. Reliable-Modus verlangt
mindestens zwei Bestätigungen.

| Methode / Pfad | Inhalt |
|---|---|
| `GET /api/status` | WiFi, MQTT, IP, RSSI, Heap, Uptime, Firmware, OTA |
| `GET /api/remotes` | Remote-Index mit Button-Metadaten |
| `GET /api/remotes/{remote}` | Eine Remote, Name URL-kodieren oder ID verwenden |
| `POST /api/remotes` | `{"name":"Ventilator"}` |
| `PATCH /api/remotes` | `{"remote":"Ventilator","name":"Fan","optimistic_switch":false}` |
| `DELETE /api/remotes/{remote}` | Remote einschließlich ihrer Buttons entfernen |
| `POST /api/learn/start` | Name, optional Remote und Lerneinstellungen |
| `POST /api/learn/cancel` | Lernsession abbrechen |
| `GET /api/learn/status` | Status, Zähler, Restzeit und Fehlermeldung |
| `POST /api/send` | `{"remote":"Ventilator","button":"Power"}`, optional `request_id`; HTTP 202 |
| `GET /api/commands` | Letzte acht Aufträge; optional `?request_id=...` |
| `POST /api/commands/cancel` | `{"request_id":"film-42"}` |
| `POST /api/macros` | Szene mit Zuordnung, Namen und Schritten anlegen/bearbeiten |
| `DELETE /api/macros` | Szene mit `remote` / `macro` löschen |
| `POST /api/macros/run` | Szene mit `remote` / `macro` einreihen |
| `PATCH /api/remotes/layout` | Layout und vollständige Button-ID-Reihenfolge |
| `POST /api/firmware` | Multipart-Firmware, Prüfheader gemäß [FEATURES_0.2](docs/FEATURES_0.2.md) |
| `POST /api/send/latest` | `{}`; letzten Empfang per RAW senden |
| `GET /api/button?remote=...&button=...` | Details, optional `&raw=1` für vollständigen IR-Code |
| `PATCH /api/button` | Remote, Button und geänderte Einstellungen |
| `DELETE /api/button` | `{"remote":"Ventilator","button":"Power"}` |
| `GET /api/received/latest` | Empfangsmetadaten, Validität und Sequenznummer |
| `POST /api/received/use` | `{"remote":"Ventilator","name":"Power","sequence":42}` |
| `GET /api/config` | Konfiguration, nur Passwort-vorhanden-Marker |
| `POST /api/config` | Konfiguration ändern und neu starten |
| `GET /api/diagnostics` | Speicher, Reconnect-Zähler, Flashgröße, IR-Zeitpunkte und Limits |
| `GET /api/ota/status` | Version, Status und Fortschritt |
| `GET /api/backup/export` | Download `irbridge-backup.json` |
| `POST /api/backup/import` | Multipart-Datei; Header `X-IRBridge-Import: 1` erforderlich |

Minimaler Lernstart aus der Aufgabenbeschreibung:

```json
{"name":"Power"}
```

Ohne Remote wird bei leerer Datenbank eine **Remote** angelegt oder die einzige
vorhandene gewählt. Bei mehreren Remotes muss `remote` angegeben werden. Der
Standard dieser API ist Fast Learn; die Weboberfläche schlägt drei Bestätigungen vor.

Ein vollständiges Beispiel:

```json
{
  "remote":"Ventilator",
  "name":"Power",
  "mode":"reliable",
  "confirmations":3,
  "tolerance_percent":20,
  "send_mode":"RAW",
  "entity_type":"button",
  "repeat_count":1
}
```

Zum erneuten Lernen zusätzlich `replace_id` mit der vorhandenen Button-ID senden.
Button-Änderungen unterstützen `name`, `entity_type`, `send_mode`, `repeat_count`,
`frequency` in Hz sowie `icon`, `slot`, `favorite`. `entity_type` verwendet intern `button`, `on`, `off`,
`toggle`, `custom`. Eine Frequenzänderung setzt `frequency_source` auf `manual`.
Senden während einer Lernsession ergibt einen Fehler; zuerst abbrechen oder
den Lernabschluss abwarten.

## LittleFS, Backup und Stromunterbrechungen

Die Datenbank verwendet Version 1. `/data/remotes.json` enthält Remotes und
Button-Metadaten mit stabilen IDs. Große IR-Daten stehen separat in
`/data/codes/cXXXXXXXX.json`, damit nicht alle RAW-Arrays zugleich im RAM liegen.
Jede Code-Datei enthält `protocol`, `bits`, Hex-Strings für `value`, `address`,
`command`, `frequency`, `frequency_source`, `repeat`, `raw_length`, `raw` und
gegebenenfalls `state` als Hex-String. Die Namen gehören zu den Indexeinträgen.
Im exportierten Backup werden Namen und vollständige Signale zusammengeführt.

Ein Speichervorgang schreibt zunächst eine neue temporäre Code-Datei, schließt
sie und prüft den vollständigen Inhalt gegen die kanonische Serialisierung.
Die bestätigte Datei erhält einen neuen unveränderlichen Namen. Erst danach
wird ein validierter temporärer Index per atomarem LittleFS-Rename eingesetzt.
Der alte Zielindex wird vorher nicht gelöscht. Nach einem unterbrochenen Vorgang
verweist der aktive Index auf die alten oder neuen vollständigen Codes; nicht
referenzierte temporäre/neue Code-Dateien werden später aufgeräumt. Konfigurations-
und Discovery-Dateien verwenden ebenfalls validiertes temporäres Schreiben.

Unter Einstellungen ein Backup herunterladen. Format:

```json
{
  "version":1,
  "configuration":{"wifi_ssid":"Mein WLAN","mqtt_host":"192.168.1.10","mqtt_port":1883},
  "remotes":[
    {
      "id":"01234567",
      "name":"Ventilator",
      "optimistic_switch":false,
      "buttons":[
        {
          "id":"89abcdef",
          "name":"Power",
          "entity_type":"button",
          "send_mode":"AUTO",
          "repeat_count":1,
          "signal":{"protocol":"RAW","bits":0,"value":"0x0","address":"0x0","command":"0x0","frequency":38000,"frequency_source":"default","repeat":false,"decoded_reliable":false,"raw_length":7,"raw":[9000,4500,560,560,560,1690,560]}
        }
      ]
    }
  ]
}
```

Das kurze RAW-Array demonstriert ausschließlich das Format und ist kein
funktionierender Ventilatorcode. Der Export enthält alle echten Daten und
Konfiguration ohne Passwortfelder. Die Marker `*_password_set` verraten nur,
ob ein Passwort vorhanden ist. Ein Backup ist deshalb keine Kopie der Zugangsdaten.

Import prüft JSON-Struktur, Version, IDs, Namen, Duplikate, Signalgrößen, Timings
und Konfiguration. Neue Code-Dateien werden zunächst isoliert angelegt. Erst
nach vollständiger Validierung wird der Remote-Index ersetzt; ein ungültiges
Backup verändert die aktive Datenbank nicht. Die vorhandenen lokalen Passwörter
und die lokale Webanmeldung bleiben erhalten. Nach einem erfolgreichen Import
startet die Bridge neu; eine neue Bridge benötigt ihre Passwörter erneut.

Remote-Index und Konfiguration sind jeweils eigene atomare Dateien. Ein Stromausfall
zwischen ihren zwei Commits kann die importierten Remotes mit der bisherigen
Konfiguration hinterlassen. Beide Dateien bleiben dabei gültig. Meldet der Import
einen Konfigurations-Speicherfehler nach Index-Commit, weist die API ausdrücklich
auf die bereits importierten IR-Daten hin. Dies ist keine Transaktion über
das gesamte Dateisystem.

## OTA und Diagnose

Unter Einstellungen erlaubt ein eigenes Formular Firmwareuploads im Browser mit
Größen-/Targetprüfung, MD5-Integrität und Fortschrittsanzeige. Vorher ein Backup
exportieren. Konfiguriertes Web- und OTA-Passwort gelten auch für diesen Weg;
Details stehen in [FEATURES_0.2](docs/FEATURES_0.2.md).

ArduinoOTA ist standardmäßig aktiviert, arbeitet nach erfolgreicher WLAN-Verbindung
und kann in den Einstellungen deaktiviert oder mit Passwort geschützt werden.
Unter Firmware werden aktuelle Version und OTA-Status angezeigt. Das OTA-Environment
ist vorbereitet; die reale IP oder den tatsächlichen Hostnamen übergeben:

```console
pio run -e esp_ir_ota
pio run -e esp_ir_ota -t upload --upload-port 192.168.1.42
```

`upload_port = irbridge.local` ist ein Beispielwert; der automatische Gerätehostname
enthält einen MAC-Suffix. Für eine passwortgeschützte OTA-Verbindung unterstützt
`scripts/ota_auth.py` die Umgebungsvariable `IRBRIDGE_OTA_PASSWORD`. Zum Beispiel
in PowerShell interaktiv setzen:

```powershell
$otaSecret = Read-Host 'OTA-Passwort' -AsSecureString
$env:IRBRIDGE_OTA_PASSWORD = [System.Net.NetworkCredential]::new('', $otaSecret).Password
pio run -e esp_ir_ota -t upload --upload-port 192.168.1.42
Remove-Item Env:IRBRIDGE_OTA_PASSWORD
```

Keine Zugangsdaten in `platformio.ini` oder Git speichern. Eine OTA-Aktualisierung
der Firmware erhält die Code-Dateien bei unverändertem Flashlayout. Eine
OTA-Dateisystemaktualisierung ersetzt sie genauso wie serielles `uploadfs` und
benötigt vorher ein Backup. Während eines OTA-Vorgangs werden IR-Empfang und
Lernen angehalten und MQTT sauber getrennt. Ein Uploadfehler aktiviert den
Empfänger erneut; bei einem Dateisystem-Upload wird LittleFS vorher ausgehängt.

`/diagnostics` zeigt Version/Builddatum, Heap/größten Block, LittleFS-Belegung,
Flashgrößen, WLAN-RSSI, WiFi-/MQTT-Reconnects, Uptime, letzte IR-Aktivität und Anzahl
Remotes/Buttons. Uptime und Ereigniszeitpunkte berücksichtigen `millis()`-Überläufe.
Serial-Logs verwenden `[WiFi]`, `[MQTT]`, `[IR-RX]`, `[IR-TX]`, `[FS]`, `[WEB]`,
`[HA]`, `[LEARN]`, `[OTA]` mit INFO/WARN/ERROR-Leveln.

## Projektstruktur und reproduzierbare Prüfungen

```text
platformio.ini
include/        config.h, Manager-Header, secrets.example.h
src/            main.cpp, ir_manager.cpp, storage.cpp, learning_manager.cpp,
                wifi_manager.cpp, mqtt_manager.cpp, discovery_manager.cpp,
                web_server.cpp, ota_manager.cpp, command_manager.cpp, system_status.cpp
lib/BridgeCore/ plattformunabhängige JSON-, Lern-, Backup- und Discovery-Logik
lib/IRCodec/    tatsächliche IRremoteESP8266-Empfangsdaten in persistentes Format
data/           index.html, app.js, md5.js, style.css
scripts/        Buildprüfung, native Tests, OTA-Passwortübergabe
test/           Core- und IR-Library-Tests
docs/           Entwicklungsprotokoll und Prüfbericht
```

Firmware und LittleFS:

```console
pio run -e esp_ir_usb -t clean
pio run
pio run -e esp_ir_usb -t buildfs
pio run -e esp_ir_ota
```

Alternativ führt `python scripts/verify_builds.py` diese vier Schritte aus und
legt Logs unter `artifacts/` ab. Das Skript verwendet ein lokales `.pio-core`,
sofern `PLATFORMIO_CORE_DIR` nicht gesetzt ist. Ein gültiger PlatformIO-Core-Pfad
oder `pio` im PATH ist erforderlich.

Die nativen Tests kompilieren denselben Core und Codec wie die Firmware; die
IR-Tests verwenden die tatsächlichen gepinnten Empfangs-/Sendealgorithmen der
Library mit deren `UNIT_TEST`-Backend. Nach mindestens einem USB-Build und mit
einem installierten `g++`:

```console
python scripts/run_native_tests.py
```

Unter Windows kann alternativ die PlatformIO-MinGW-Toolchain lokal installiert
werden (danach für normale Firmware-Builds wieder `.pio-core` verwenden):

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.tools/native-core'
pio pkg install -g -t platformio/toolchain-gccmingw32
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.pio-core'
python scripts/run_native_tests.py
```

Der Test-Runner erkennt `g++` im PATH oder die lokale MinGW-Toolchain. Die Tests
decken unter anderem NEC und unbekanntes RAW, Daikin-Full-State, sehr lange
Pakete, JSON-/Backup-Validierung, gehaltene Tasten, Reconnect-Zeitsteuerung,
HA-Discovery/-Löschung und simulierte Unterbrechungen des atomaren Speicherns ab.
Sie ersetzen keine Messung auf dem ESP8266 und keinen Test mit einem realen
Home-Assistant-Broker. Details und die verbleibende Hardware-Prüfliste stehen
im [Prüfbericht](docs/TEST_REPORT.md).

Zusätzliche MQTT-TCP-Integrationstests verwenden die Produktionsdateien
`mqtt_manager.cpp`, `command_manager.cpp` und `discovery_manager.cpp`, PubSubClient und einen lokalen
[aMQTT-Broker](https://amqtt.readthedocs.io/en/latest/references/broker/).
WiFiClient ist dabei durch Host-TCP-Sockets, LittleFileOps durch Hostdateien
und die physische IR-Ausgabe durch einen Spy adaptiert. Die Firmware benötigt
keines der Python-Testpakete. Für diese Hostprüfung mit Python 3.10 oder neuer:

```console
python -m pip install --target .tools/mqtt-python -r scripts/mqtt-test-requirements.txt
python scripts/run_mqtt_tests.py
python scripts/check_web_assets.py
python scripts/run_web_tests.py
python scripts/package_release.py
```

Die Tests starten ausschließlich einen Broker auf `127.0.0.1` mit einem freien
Port und beenden ihre Prozesse danach. Sie verändern weder einen externen Broker
noch die Platine. Ein JavaScript-Interpreter `node` wird für die Syntaxprüfung
benötigt; die Prüfung meldet ausdrücklich, wenn er fehlt. Die statische Prüfung
öffnet keinen Browser. `run_web_tests.py` verwendet zusätzlich ein isoliertes
Headless-Chromium mit einem lokalen Fixture-Server. Chrome/Chromium oder Edge
müssen installiert sein (alternativ `CHROME_PATH` setzen). `package_release.py` prüft Build-/
Testlogs und die Aktualität der Images vor dem Verpacken und kontrolliert die
ZIP-CRCs; alle Paketdateien sind in `artifacts/` von Git ausgeschlossen.
