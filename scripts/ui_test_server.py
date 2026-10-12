"""Stateful local UI fixture; no ESP hardware or real authentication is simulated."""
import hashlib
import json
import time
import argparse
from email import policy
from email.parser import BytesParser
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parents[1]
TARGET = "IRBRIDGE:ESP8266:4M1M:v1"
REMOTES = [dict(id="00000001", name="Ventilator", optimistic_switch=False, layout="grid", macros=[],
    buttons=[dict(id="00000002",name="Power",entity_type="toggle",send_mode="RAW",repeat_count=1,icon="power",slot="power",favorite=False),
             dict(id="00000003",name="Speed +",entity_type="button",send_mode="AUTO",repeat_count=1,icon="fan",slot="",favorite=False)])]
CONFIG = dict(wifi_ssid="Test network",hostname="irbridge-test",mqtt_host="192.168.1.2",mqtt_port=1883,
    mqtt_user="",discovery_prefix="homeassistant",ota_enabled=True,web_auth=False,web_user="admin",ap_fallback_enabled=True)
LEARN = dict(status="idle",captured=0,required=3,message="",remaining_ms=30000)
COMMANDS = []
SIGNAL = dict(protocol="NEC",bits=32,value="0x20DF10EF",address="0x4",command="0x8",frequency=38000,
    frequency_source="protocol_default",repeat=False,decoded_reliable=True,raw_length=67)
counter = 16

def new_id():
    global counter
    counter += 1
    return f"{counter:08x}"

def remote_by_id(value):
    return next((r for r in REMOTES if r["id"] == value or r["name"] == value),None)

def advance():
    now = time.monotonic()
    for command in COMMANDS:
        if command["status"] == "queued" and now >= command["finish_at"]:
            command.update(status="sent",completed_steps=command["total_steps"])
    if LEARN["status"] == "waiting" and now >= LEARN["finish_at"]:
        remote = remote_by_id(LEARN["remote"])
        button = next((b for b in remote["buttons"] if b["id"] == LEARN.get("replace_id")),None)
        if button: button["name"] = LEARN["name"]
        else: remote["buttons"].append(dict(id=new_id(),name=LEARN["name"],entity_type="button",send_mode="AUTO",repeat_count=1,icon="",slot="",favorite=False))
        LEARN.update(status="saved",captured=LEARN["required"],message="UI fixture: Signal gespeichert")

class Handler(BaseHTTPRequestHandler):
    def send_json(self,obj,code=200):
        self.send_response(code)
        self.send_header("Content-Type","application/json")
        self.send_header("Cache-Control","no-store")
        self.end_headers()
        self.wfile.write(json.dumps(obj).encode())

    def fail(self,message,code=400):
        self.send_json(dict(success=False,error=message),code)

    def read_body(self):
        return json.loads(self.rfile.read(int(self.headers.get("Content-Length","0"))) or "{}")

    def do_GET(self):
        advance()
        parsed = urlparse(self.path)
        path,query = parsed.path,parse_qs(parsed.query)
        if path == "/api/status":
            return self.send_json(dict(firmware_version="0.2.0",build_date="UI fixture",device_id="aabbccddeeff",hostname="irbridge-test",
                wifi_connected=True,ap_active=False,ap_protected=True,ap_fallback_enabled=CONFIG["ap_fallback_enabled"],ip="192.168.1.123",rssi=-46,
                uptime_ms=1234567,free_heap=30000,max_free_block=22000,wifi_reconnect_count=2,mqtt_connected=True,mqtt_reconnect_count=1,
                ota_status="ready",storage_ready=True,remote_count=len(REMOTES),button_count=sum(len(r["buttons"]) for r in REMOTES),
                littlefs_total=1024000,littlefs_used=64000,last_ir_received_ms=1220000,last_ir_transmitted_ms=1230000,last_send_mode="RAW",
                max_remotes=16,max_buttons=64,rx_buffer_size=4096,max_raw_samples=8192,backup_supported=True,discovery_status="synced",
                queued_commands=sum(c["status"]=="queued" for c in COMMANDS),firmware_max_bytes=1044463,firmware_target=TARGET,web_update_enabled=CONFIG["ota_enabled"]))
        if path == "/api/remotes": return self.send_json(dict(version=1,remotes=REMOTES))
        if path == "/api/config": return self.send_json({k:v for k,v in CONFIG.items() if "password" not in k})
        if path == "/api/learn/status": return self.send_json({k:v for k,v in LEARN.items() if k!="finish_at"})
        if path == "/api/commands":
            if "request_id" in query:
                result = next((c for c in COMMANDS if c["request_id"]==query["request_id"][0]),None)
                if not result: return self.fail("Request not found",404)
                return self.send_json({k:v for k,v in result.items() if k!="finish_at"})
            return self.send_json(dict(pending=sum(c["status"]=="queued" for c in COMMANDS),commands=[{k:v for k,v in c.items() if k!="finish_at"} for c in COMMANDS[-8:]]))
        if path == "/api/received/latest": return self.send_json(dict(sequence=1,received_ms=1220000,available=True,valid=True,error="",signal=SIGNAL))
        if path == "/api/button":
            remote = remote_by_id(query.get("remote",[""])[0])
            button = next((b for b in remote["buttons"] if b["id"]==query.get("button",[""])[0]),None) if remote else None
            if not button: return self.fail("Button not found",404)
            return self.send_json(dict(button,**SIGNAL,last_transmitted_ms=1230000))
        if path == "/api/backup/export": return self.send_json(dict(version=1,configuration={k:v for k,v in CONFIG.items() if "password" not in k},remotes=REMOTES))
        if path.startswith("/api/"): return self.fail("Unknown fixture endpoint",404)
        asset = path.lstrip("/") if path in ("/app.js","/md5.js","/style.css") else "index.html"
        file = ROOT / "data" / asset
        self.send_response(200)
        self.send_header("Content-Type","application/javascript" if file.suffix==".js" else "text/css" if file.suffix==".css" else "text/html; charset=utf-8")
        self.send_header("Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(file.read_bytes())

    def do_POST(self):
        advance()
        if self.path == "/api/firmware":
            raw = self.rfile.read(int(self.headers.get("Content-Length","0")))
            message = BytesParser(policy=policy.default).parsebytes(("Content-Type: "+self.headers["Content-Type"]+"\r\nMIME-Version: 1.0\r\n\r\n").encode()+raw)
            files = [part.get_payload(decode=True) for part in message.iter_parts()]
            if len(files)!=1: return self.fail("Expected one firmware file")
            image = files[0]
            if self.headers.get("X-Firmware-MD5")!=hashlib.md5(image).hexdigest() or self.headers.get("X-Firmware-Size")!=str(len(image)) or TARGET.encode() not in image:
                return self.fail("Invalid firmware transfer")
            return self.send_json(dict(success=True))
        body = self.read_body()
        if self.path in ("/api/send","/api/macros/run"):
            remote = remote_by_id(body.get("remote"))
            if not remote: return self.fail("Remote not found")
            steps = next((m["steps"] for m in remote["macros"] if m["id"]==body.get("macro")),None) if "macro" in body else [dict(delay_ms=0)]
            if not steps: return self.fail("Scene not found")
            request_id = body.get("request_id") or new_id()
            if any(c["request_id"]==request_id for c in COMMANDS): return self.fail("Request ID already used")
            result = dict(success=True,request_id=request_id,source="web",status="queued",completed_steps=0,total_steps=len(steps),updated_ms=1234567)
            COMMANDS.append(dict(result,finish_at=time.monotonic()+0.35+sum(s.get("delay_ms",0) for s in steps)/1000))
            return self.send_json(result,202)
        if self.path == "/api/commands/cancel":
            for command in COMMANDS:
                if command["request_id"]==body.get("request_id"): command.update(status="failed",success=False,error="Cancelled")
        elif self.path == "/api/remotes":
            REMOTES.append(dict(id=new_id(),name=body["name"],layout=body.get("layout","grid"),optimistic_switch=False,buttons=[],macros=[]))
        elif self.path == "/api/learn/start":
            if LEARN["status"]=="waiting": return self.fail("Learning already active",409)
            LEARN.update(body,status="waiting",captured=0,required=body.get("confirmations",3),message="UI fixture: warte auf Signal",finish_at=time.monotonic()+1)
        elif self.path == "/api/learn/cancel": LEARN.update(status="idle")
        elif self.path == "/api/macros":
            remote = remote_by_id(body["remote"])
            scene = dict(id=body.get("id") or new_id(),name=body["name"],steps=body["steps"])
            remote["macros"] = [m for m in remote["macros"] if m["id"]!=scene["id"]]+[scene]
        elif self.path == "/api/config": CONFIG.update(body)
        self.send_json(dict(success=True))

    def do_PATCH(self):
        body = self.read_body()
        remote = remote_by_id(body.get("remote"))
        if not remote: return self.fail("Remote not found")
        if self.path == "/api/button":
            button = next(b for b in remote["buttons"] if b["id"]==body["button"])
            button.update({k:v for k,v in body.items() if k not in ("remote","button")})
        else:
            remote.update({k:v for k,v in body.items() if k in ("name","layout","optimistic_switch")})
            if "button_order" in body: remote["buttons"] = [next(b for b in remote["buttons"] if b["id"]==id_) for id_ in body["button_order"]]
        self.send_json(dict(success=True))

    def do_DELETE(self):
        body = self.read_body()
        if self.path.startswith("/api/remotes/"): REMOTES[:] = [r for r in REMOTES if r["id"]!=self.path.rsplit("/",1)[1]]
        else:
            remote = remote_by_id(body.get("remote"))
            if self.path == "/api/button": remote["buttons"] = [b for b in remote["buttons"] if b["id"]!=body["button"]]
            elif self.path == "/api/macros": remote["macros"] = [m for m in remote["macros"] if m["id"]!=body["macro"]]
        self.send_json(dict(success=True))

if __name__=="__main__":
    parser=argparse.ArgumentParser(); parser.add_argument("--port",type=int,default=8765)
    port=parser.parse_args().port
    print(f"UI fixture http://127.0.0.1:{port} (no hardware simulation)",flush=True)
    ThreadingHTTPServer(("127.0.0.1",port),Handler).serve_forever()
