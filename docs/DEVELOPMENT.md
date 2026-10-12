# Entwicklungsprotokoll

Die Phasen werden nacheinander implementiert und mit PlatformIO geprüft.
Messungen an echter Hardware werden gesondert von Build- und Hosttests geführt.

- Phase 1: Minimalprojekt, zentrale Pins/Version, USB-/OTA-Environments und feste
  Library-Versionen. `pio run -e esp_ir_usb`: **SUCCESS** (265695 Byte Flash).

- Phase 2: IR-Empfang mit allen Library-Decodern, RAW-Erfassung, Zustandsbytes und Overflow-Pruefung. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 3: Sender, RAW-Replay des letzten Signals, AUTO-DECODED-RAW und RX-Echosperre. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 4: LittleFS, Remote-Index, einzelne Signaldateien, Streaming-JSON und atomare validierte Speicherung. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 5: Fast-Reliable-Learning mit Timingtoleranz, echten getrennten Tastendruecken, Timeout und dauerhaftem Replay. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 6: Responsives lokales Webinterface, REST-API, Live-Monitor, Basic-Auth und WLAN-Setupportal mit Reconnect. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 7: MQTT-Befehle, Empfangsereignisse, Last Will, Telemetrie und Reconnect. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 8: HA-Discovery mit stabilen IDs, persistenten Loeschungen, ON-OFF-Paaren und optionalen optimistischen Switches. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 9: Validierter Streaming-Import-Export, passwortfreie Backups und vollstaendige Diagnose-API. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 10: ArduinoOTA mit optionalem Passwort, Heap-Optimierung, Langzeit-Uptime und Stabilisierung. `pio run -e esp_ir_usb`: **SUCCESS**.

- Phase 10: Stabilisierung: echte IR-Codec-Tests, streamingbasierte Schreibvalidierung und sparsamer Heap. `pio run -e esp_ir_usb`: **SUCCESS**.

- Abschlussprüfung: `pio run -e esp_ir_usb -t clean`, anschließend `pio run`:
  **SUCCESS**, 541224 Byte Flash / 41528 Byte statischer RAM.
  LittleFS-Image mit `buildfs`: **SUCCESS**. OTA-Environment: **SUCCESS**.
  Gemeinsamer Firmware-Core: **16/16 Testgruppen bestanden**.
  Tatsächlicher IR-Codec mit IRremoteESP8266-Sende-/Empfangsalgorithmen:
  **5/5 Testgruppen bestanden**. JavaScript-Syntax: **bestanden**.
  Noch offene Hardware-/Netzwerkprüfungen sind in `TEST_REPORT.md` aufgeführt.

- Abschließende Stabilisierung: Backup-Export bricht bei fehlendem JSON-Heap
  ab; nach fehlgeschlagenem Dateisystem-OTA werden LittleFS und die Datenbank
  erneut ohne Autoformat geprüft. Clean-USB-/LittleFS-/OTA-Builds anschließend
  erneut **SUCCESS**. Statische Web-/README-Konsistenzprüfung **bestanden**.

- Abschlussstufe 1: Reliable Learn vergleicht die logischen Timings auch über
  uint16-Splitgrenzen hinweg. Importfelder sind typisiert, RAW-Allokationen
  beachten das Speicherbudget, AUTO prüft Dekodierung und Frequenz erneut.
  `pio run`: **SUCCESS** (542816 Byte Flash / 41768 Byte statischer RAM).

- Abschlussstufe 2: API-/MQTT-Feldvalidierung, sichere Lernparameter und Erhalt
  nicht geänderter Remote-Einstellungen. `pio run`: **SUCCESS**
  (544956 Byte Flash / 42004 Byte statischer RAM). Produktions-MQTT- und
  Discovery-Manager mit tatsächlichem PubSubClient an lokalem TCP-Broker geprüft:
  **8/8 Integrationsgruppen bestanden**, inklusive LWT, Reconnect und Offline-Löschung.

- Abschlussstufe 3: NUL-Zeichen werden vor C-String-Konvertierungen abgelehnt.
  Erneute vollständige Clean-USB-, LittleFS- und OTA-Builds: **SUCCESS**
  (545236 Byte Flash / 42036 Byte statischer RAM).
  **22/22 Core-, 5/5 IR-Library- und 8/8 TCP-Integrationsgruppen bestanden**.
  Webassets und JavaScript-Syntax geprüft; reproduzierbare Prüfscripts und
  Paketierung mit Images, Logs und SHA-256-Manifest ergänzt.

- Erweiterung 0.2.0: atomar gespeicherte Szenen mit stabilen Referenzen und
  konfigurierbaren Pausen, FIFO-Sendewarteschlange mit Request-IDs, Ergebnissen
  und Abbruch; geführte Tastenfolge im Browser; Layouts, Icons, Favoriten und
  Reihenfolge; fünf HA-Diagnosesensoren; validierte Firmwareuploads im Browser;
  zufälliger gerätelokaler Recovery-Schlüssel und konfigurierbarer AP-Fallback.
  Additive Daten-/Backupfelder erhalten vorhandene Remotes und Button-IDs.
  USB-/LittleFS-/OTA-Builds und 29 Core-, 5 IR-Library-, 11 TCP- sowie 9 lokale
  Browsergruppen bestanden. Screenshots für Desktop/Mobil visuell geprüft.
  Hardwareabnahme bleibt gemäß TEST_REPORT.md offen. Details: FEATURES_0.2.md.
- Recovery-WLAN: zufällige Passwortvergabe durch das feste Standardpasswort
  `05102001` ersetzt. Ein eigenes Passwort bleibt in Einstellungen konfigurierbar.
