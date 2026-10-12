# ESP IR Bridge 0.2 · Szenen und Bedienung

## Szenen und Sendewarteschlange

Auf der Hauptseite **+ Szene** wählen, eine Remote als Zuordnung und einen Namen
eintragen. Jeder Schritt verweist auf eine bereits gelernte Taste. Die Pause in
Millisekunden liegt **vor** diesem Schritt; auch Tasten anderer Remotes sind
erlaubt. Schritte können verschoben, hinzugefügt und entfernt werden.

Bis zu 12 Szenen insgesamt, 16 Schritte je Szene, 60000 ms pro Pause und
300000 ms Pause insgesamt sind erlaubt. Zusätzlich gelten das bestehende
Indexlimit von 16000 Bytes und das JSON-Speicherbudget. Namen und Zuordnung
ändern die stabilen Tasten-IDs nicht. Das Löschen einer von einer Szene benutzten
Taste/Remote wird abgelehnt; zunächst den betreffenden Schritt entfernen.
Szenen erscheinen auch als Home-Assistant-Buttons.

Gelernte Tasten und Szenen werden über eine gemeinsame FIFO-Warteschlange mit
höchstens sechs Aufträgen gesendet. Ein Auftrag enthält IDs statt RAW-Kopien;
die Firmware lädt nur den gerade benötigten Code. Pausen verwenden den
Hauptloop. Mindestens 40 ms trennen zwei Übertragungen. Die eigentliche
IR-Ausgabe der Library bleibt synchron und kann bei langen Paketen oder vielen
Wiederholungen HTTP/MQTT-Verarbeitung kurzzeitig verzögern.

`POST /api/send` und `POST /api/macros/run` antworten mit **HTTP 202**:

```json
{"success":true,"request_id":"film-42","source":"web","status":"queued","completed_steps":0,"total_steps":2,"updated_ms":12345}
```

Die drei Auftragszustände sind `queued`, `sent` und `failed`. `queued` wird auch
nach einem erfolgreich ausgeführten Zwischenschritt gemeldet. `sent` bestätigt
die abgeschlossene IR-Ausgabe aller Schritte, ohne Rückmeldung des Zielgeräts.
Ein Fehler beendet die Szene; bereits gesendete Schritte werden nicht rückgängig
gemacht. `completed_steps` nennt deren Anzahl. Die Hauptseite wartet für ihre
Erfolgsmeldung auf `sent`. Unter **Diagnose → Letzte Befehle** sind Ergebnisse
und die Abbruchaktion sichtbar.

Die letzten acht Ergebnisse liegen ausschließlich im RAM. Warteschlange,
Ergebnisse und Schutz vor wiederverwendeten Request-IDs werden bei einem
Neustart verworfen. Wiederverwendete IDs werden abgelehnt, solange sie in dieser
Historie liegen. Das ist keine dauerhafte Deduplizierung. MQTT-Ergebnisse sind
nicht retained und werden ohne garantierte Zustellung publiziert; die REST-API
bietet Zugriff auf die noch vorhandene Historie.

| API | Zweck |
|---|---|
| `POST /api/send` | Taste oder Szene mit optionalem `request_id` einreihen |
| `POST /api/macros` | Szene anlegen; mit bestehender `id` bearbeiten |
| `DELETE /api/macros` | Mit `remote` und `macro` löschen |
| `POST /api/macros/run` | Szene einreihen |
| `GET /api/remotes` | Remotes einschließlich `macros` und Layoutdaten |
| `GET /api/commands` | Letzte Ergebnisse und Zahl wartender Aufträge |
| `GET /api/commands?request_id=film-42` | Einen vorhandenen Auftrag prüfen |
| `POST /api/commands/cancel` | Mit `request_id` abbrechen; Resultat `failed` / `Cancelled` |

Neue Szene, `POST /api/macros`:

```json
{
  "remote":"00000001",
  "name":"Filmabend",
  "steps":[
    {"remote":"00000001","button":"00000002","delay_ms":0},
    {"remote":"00000004","button":"00000005","delay_ms":1500}
  ]
}
```

Die IDs im Beispiel durch IDs aus `/api/remotes` ersetzen. Die Szenenschritte
verwenden ausschließlich stabile IDs. Ein Sendebefehl erlaubt dagegen Namen
oder IDs, sowohl per HTTP als auch auf `irbridge/<device_id>/send`:

```json
{"remote":"Ventilator","macro":"Filmabend","request_id":"film-42"}
```

Auf `irbridge/<device_id>/command` kann zusätzlich `action: "macro"` oder
`action: "send"` angegeben werden. Ergebnisse erscheinen auf
`irbridge/<device_id>/command/result` mit der passenden `request_id`. Request-IDs
enthalten 1–48 ASCII-Buchstaben, Ziffern, Punkte, Bindestriche oder Unterstriche.
Ohne ID erzeugt die Bridge eine ID. Befehle weiterhin **ohne Retain** senden.

Lernen und RAW-Replay des letzten Empfangs verlangen eine leere Warteschlange.
Konfigurationsänderungen mit Neustart und Backup-Import verlangen zusätzlich
eine abgeschlossene Lernsession. Ein ArduinoOTA-Start beendet wartende Aufträge
mit einem Fehler. Abbruch wirkt zwischen Übertragungen; eine laufende synchrone
IR-Ausgabe lässt sich dadurch nicht unterbrechen.

## Mehrere Tasten lernen

**Mehrere Tasten lernen** oder **Tastenfolge** an einer Remote öffnen. Neue,
eindeutige Namen zeilenweise eingeben, Remote und Lernmodus wählen. Nach jeder
gespeicherten Aufnahme **Taste testen**, anschließend **Nächste Taste** wählen.
**Erneut versuchen** lernt die aktuelle Taste neu und erhält eine bereits
vergebene ID. **Überspringen** und **Beenden** erhalten alle schon gespeicherten
Tasten. Der Assistent läuft im aktuellen Browser; ein Neuladen verwirft die
noch ausstehende Namensliste. Auf dem Gerät gespeicherte Tasten bleiben erhalten.

## Anordnung, Symbole und Favoriten

Im Remote-Menü die Anordnung **Tastenraster**, **Richtungstasten** oder
**Mediensteuerung** wählen. In den Tastendetails **Symbol**, **Position in der
Remote** und **Als Favorit anzeigen** einstellen. Eine Position darf je Remote
nur einmal vergeben werden. Nicht zugeordnete Tasten stehen unter den festen
Bedienelementen. Favoriten erscheinen gemeinsam oberhalb der Remotes.
**Nach vorne** / **Nach hinten** im Tastenmenü verändert die gespeicherte
Reihenfolge; feste Positionen wie „Hoch“ werden dadurch nicht verschoben.

`PATCH /api/remotes/layout` nimmt `remote`, optional `layout` und
`button_order` entgegen. Die Reihenfolge muss jede Button-ID genau einmal
enthalten. `PATCH /api/button` unterstützt zusätzlich `icon`, `slot` und
`favorite`. Layouts und Szenen werden im bestehenden atomaren Index gespeichert
und in Backups aufgenommen. Reine Layout-/Metadatenänderungen schreiben keine
neue IR-Signaldatei.

## Home-Assistant-Diagnose

Fünf MQTT-Sensoren werden demselben Bridge-Gerät zugeordnet: WLAN-RSSI in dBm,
freier Heap in Bytes, Uptime in Sekunden sowie WLAN- und MQTT-Reconnect-Zähler.
Sie verwenden `irbridge/<device_id>/info`, die bestehende Availability und
`entity_category: "diagnostic"`. Telemetrie erscheint alle 30 Sekunden; Sensoren
haben `expire_after: 90`. Die Discovery-Historie erfasst auch diese Entities und
Szenen. Sensoren zeigen den Softwarezustand der Bridge, nicht den Zustand eines
per IR gesteuerten Geräts.

## Firmware im Browser

Unter Einstellungen zuerst ein Backup exportieren, dann im eigenen
Firmwareformular eine unkomprimierte `firmware.bin` für dieses Projekt wählen.
Bei konfiguriertem OTA-Passwort dieses im Uploadformular eingeben. Web/API-
Authentifizierung gilt ebenfalls. `ota_enabled: false` deaktiviert beide
Updatewege. Während des Browseruploads müssen Lernen und Warteschlange leer sein.

Browser und Firmware prüfen Größe und ESP8266-Header. Das Gerät prüft den
4-MB-Flashheader, die tatsächliche Flashgröße, den Projektmarker
`IRBRIDGE:ESP8266:4M1M:v1`, die vollständige Länge und MD5-Transferintegrität.
Der Updater validiert das Image vor Aktivierung. Ein falsches, unvollständiges
oder abgebrochenes Image wird nicht aktiviert. MD5 und Projektmarker sind keine
Signaturprüfung. Nach Erfolg erfolgt ein Neustart; LittleFS wird nicht ersetzt.

Für eigene Clients erwartet `POST /api/firmware` genau eine Multipart-Datei im
Feld `firmware`, deren Name auf `.bin` endet, mit folgenden Headern:

| Header | Inhalt |
|---|---|
| `X-IRBridge-Firmware` | `1` |
| `X-Firmware-Size` | Tatsächliche Dateigröße in Bytes |
| `X-Firmware-MD5` | 32 Hexzeichen des vollständigen Images |
| `X-IRBridge-OTA-Password` | Konfiguriertes OTA-Passwort, falls vorhanden |

Der Fortschrittsbalken zeigt den HTTP-Upload; erst die erfolgreiche Antwort
bestätigt die Prüfung. Der Updater schreibt in den freien Firmwarebereich,
aktiviert das Image erst nach Abschluss und rebootet danach. Es gibt keine
automatische Rückkehr zu einer älteren Firmware nach deren Start.

## Passwortgeschütztes Recovery-WLAN

Das Standardpasswort ist **`05102001`**. Damit zum Netz `IRBridge-ABC123`
verbinden und `http://192.168.4.1/settings` öffnen. Es wird kein zufälliger
Schlüssel erzeugt oder aus einer früheren `/data/recovery.json` geladen.

**Neues Recovery-WLAN-Passwort** erlaubt einen eigenen Schlüssel mit 8–63
druckbaren ASCII-Zeichen. Ein leeres UI-Feld behält den bisherigen Schlüssel.
`recovery_password: ""` per API wählt wieder das Standardpasswort `05102001`.
**Recovery-WLAN bei Verbindungsausfall aktivieren** steuert den automatischen
Fallback nach 45 Sekunden. Ersteinrichtung und eine nicht verfügbare Datenbank
starten weiterhin das geschützte AP. Der serielle Befehl `a` öffnet es auch
manuell. Das Standardpasswort gilt auch bei nicht verfügbarem Dateisystem.

## Update eines bestehenden 0.1-Geräts

0.2 liest vorhandene Datenbanken und 0.1-Backups mit Standardlayout und ohne
Szenen. Vor der Umstellung Backup exportieren. Für die neuen Webdateien wird
auch das neue LittleFS-Image benötigt; dessen Upload ersetzt alle Laufzeitdaten.
Firmware und LittleFS hochladen, mit dem Passwort `05102001` verbinden,
WLAN einrichten und das Backup importieren. Danach neue Web-
und MQTT-Zugangsdaten bei Bedarf erneut konfigurieren. Neuere Backups mit
Layout-/Szenenfeldern sind für Firmware 0.2 vorgesehen.

Die Build-/Host-/Browserprüfungen und die noch offenen Versuche auf dem echten
Board stehen in [TEST_REPORT.md](TEST_REPORT.md).
