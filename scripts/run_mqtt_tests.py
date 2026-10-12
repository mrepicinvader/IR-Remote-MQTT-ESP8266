"""Compile production MQTT/Discovery managers and test them over real localhost TCP.

Only leaf hardware interfaces are replaced: TCP sockets for WiFiClient, host
files for LittleFileOps, fixture WiFi/telemetry and an IR transmit spy. The
production managers, PubSubClient, JSON, discovery ledger and scheduler are used.
No ESP, external MQTT broker or physical IR device is modified.
"""
from pathlib import Path
import json
import os
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/mqtt-python"))
try:
    import paho.mqtt.client as mqtt
    import amqtt
except ImportError:
    sys.exit("Installiere Testpakete: python -m pip install --target .tools/mqtt-python -r scripts/mqtt-test-requirements.txt")

BASE = "irbridge/aabbccddeeff"
DISCOVERY = "homeassistant/button/irbridge_aabbccddeeff/00000002/config"
OUT = ROOT / "test/results"

def wait_until(predicate, timeout=12, description="condition"):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"Timeout: {description}")

class Broker:
    def __init__(self, port):
        self.port = port
        self.process = None
        self.log = (OUT / "mqtt-broker.log").open("a", encoding="utf-8")

    def start(self):
        self.process = subprocess.Popen([sys.executable, str(ROOT / "scripts/mqtt_test_broker.py"), str(self.port)],
                                        cwd=ROOT, stdout=subprocess.PIPE, stderr=self.log, text=True)
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(self.process.stdout.readline()), daemon=True).start()
        if ready.get(timeout=10).strip() != "READY":
            raise AssertionError("Broker start failed; see test/results/mqtt-broker.log")

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)
        self.process = None

class Observer:
    def __init__(self, port):
        self.port = port
        self.messages = []
        self.lock = threading.Lock()
        self.ready = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="irbridge-test-" + uuid.uuid4().hex)
        self.client.on_connect = self.connected
        self.client.on_subscribe = lambda *_: self.ready.set()
        self.client.on_disconnect = lambda *_: self.ready.clear()
        self.client.on_message = self.received
        self.client.reconnect_delay_set(1, 1)
        self.client.connect("127.0.0.1", port, 10)
        self.client.loop_start()
        assert self.ready.wait(5), "Observer could not subscribe"

    def connected(self, client, userdata, flags, reason, properties):
        assert not reason.is_failure, str(reason)
        client.subscribe([(BASE + "/#", 1), ("homeassistant/#", 1)])

    def received(self, client, userdata, message):
        with self.lock:
            self.messages.append((time.monotonic(), message.topic, bytes(message.payload), message.retain))

    def matching(self, topic, predicate=lambda _: True, since=0):
        with self.lock:
            return [m for m in self.messages if m[0] >= since and m[1] == topic and predicate(m[2])]

    def message(self, topic, predicate=lambda _: True, since=0, timeout=12):
        return wait_until(lambda: self.matching(topic, predicate, since), timeout, topic)[-1]

    def publish(self, suffix, payload):
        data = payload if isinstance(payload, str) else json.dumps(payload)
        info = self.client.publish(BASE + "/" + suffix, data, qos=1, retain=False)
        info.wait_for_publish(timeout=3)

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()

class Host:
    def __init__(self, exe, port, directory, env):
        self.exe, self.port, self.directory, self.env = exe, port, directory, env
        self.process = None
        self.responses = queue.Queue()
        self.log = (OUT / "mqtt-native.log").open("a", encoding="utf-8")

    def start(self):
        self.responses = queue.Queue()
        self.process = subprocess.Popen([str(self.exe), str(self.port), str(self.directory)], cwd=ROOT, env=self.env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, text=True, bufsize=1)
        def read_output():
            for line in self.process.stdout:
                self.responses.put(json.loads(line))
        threading.Thread(target=read_output, daemon=True).start()

    def command(self, text):
        assert self.process.poll() is None, "Native manager process exited"
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def status(self):
        self.command("status")
        return self.responses.get(timeout=5)

    def online(self):
        return wait_until(lambda: self.status()["connected"], description="firmware MQTT online")

    def stop(self, abrupt=False):
        if self.process and self.process.poll() is None:
            if abrupt:
                self.process.terminate()
            else:
                self.command("quit")
            self.process.wait(timeout=5)
        self.process = None

def compile_host():
    compiler = shutil.which("g++")
    if not compiler:
        candidate = ROOT / ".tools/native-core/packages/toolchain-gccmingw32/bin/g++.exe"
        if candidate.exists():
            compiler = str(candidate)
    if not compiler:
        sys.exit("g++ fehlt; Einrichtung siehe README.")
    pubsub = ROOT / ".pio/libdeps/esp_ir_usb/PubSubClient/src"
    arduinojson = ROOT / ".pio/libdeps/esp_ir_usb/ArduinoJson/src"
    if not pubsub.is_dir():
        sys.exit("Zuerst pio run ausfuehren.")
    sources = [*sorted((ROOT / "lib/BridgeCore/src").glob("*.cpp")), pubsub / "PubSubClient.cpp",
               ROOT / "src/mqtt_manager.cpp", ROOT / "src/discovery_manager.cpp", ROOT / "src/command_manager.cpp",
               ROOT / "test/host/tcp_client.cpp", ROOT / "test/mqtt_tcp_host.cpp"]
    exe = OUT / ("mqtt_tcp_tests.exe" if os.name == "nt" else "mqtt_tcp_tests")
    command = [compiler, "-std=c++1z", "-O1", "-pthread", "-DESP8266", "-DNOMINMAX",
               *["-I" + str(path) for path in [ROOT / "test/host", ROOT / "include", ROOT / "lib/BridgeCore/src", pubsub, arduinojson]],
               *map(str, sources), "-o", str(exe)]
    if os.name == "nt":
        command.append("-lws2_32")
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    return exe, env

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe, env = compile_host()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    directory = OUT / ("mqtt-ledger-" + uuid.uuid4().hex[:16])
    directory.mkdir()
    broker, observer, host = Broker(port), None, Host(exe, port, directory, env)
    results = []
    def passed(name):
        line = "PASS " + name
        results.append(line)
        print(line, flush=True)
    try:
        broker.start()
        observer = Observer(port)
        host.start()
        host.online()
        original = json.loads(observer.message(DISCOVERY)[2])
        probe = Observer(port)
        try:
            assert probe.message(BASE + "/status", lambda p: p == b"online")[3]
            assert json.loads(probe.message(BASE + "/info")[2])["firmware_version"] == "0.2.0"
            assert probe.message(DISCOVERY)[3]
            assert original["device"]["identifiers"] == ["irbridge_aabbccddeeff"]
        finally:
            probe.close()
        passed("Actual PubSubClient TCP connection, retained online/info and production HA discovery")

        for payload, expected in [({"remote": "Ventilator", "button": "Power"}, True),
                                  ({"remote": "Ventilator", "button": "Missing"}, False), ("invalid JSON", False)]:
            since = time.monotonic()
            observer.publish("send", payload)
            response = json.loads(observer.message(BASE + "/command/result", lambda p: json.loads(p).get("status")!="queued", since=since)[2])
            assert response["success"] == expected
        assert host.status()["send_calls"] == 1
        for bad_action in [False, "send\x00extra"]:
            since = time.monotonic()
            observer.publish("command", {"action": bad_action, "remote": "Ventilator", "button": "Power"})
            assert not json.loads(observer.message(BASE + "/command/result", since=since)[2])["success"]
        assert host.status()["send_calls"] == 1, "Malformed action executed a command"
        passed("Production MQTT send dispatch and malformed command rejection; physical TX is a spy")

        since = time.monotonic()
        observer.publish("send", {"remote":"Ventilator", "button":"Power", "request_id":"trace-1"})
        observer.message(BASE + "/command/result", lambda p: json.loads(p).get("request_id")=="trace-1" and json.loads(p).get("status")=="queued", since)
        completed = json.loads(observer.message(BASE + "/command/result", lambda p: json.loads(p).get("request_id")=="trace-1" and json.loads(p).get("status")=="sent", since)[2])
        assert completed["completed_steps"] == completed["total_steps"] == 1
        passed("Request IDs correlate queued and sent MQTT results")

        since = time.monotonic()
        scene_payload = {"action":"macro", "remote":"Ventilator", "macro":"Double", "request_id":"scene-1"}
        observer.publish("command", scene_payload)
        completed = json.loads(observer.message(BASE + "/command/result", lambda p: json.loads(p).get("request_id")=="scene-1" and json.loads(p).get("status")=="sent", since)[2])
        assert completed["completed_steps"] == 2 and host.status()["send_calls"] == 4
        since = time.monotonic()
        observer.publish("command", scene_payload)
        failure = json.loads(observer.message(BASE + "/command/result", lambda p: json.loads(p).get("request_id")=="scene-1" and json.loads(p).get("status")=="failed", since)[2])
        assert "already used" in failure["error"] and host.status()["send_calls"] == 4
        passed("MQTT scenes execute in order and duplicate recent request IDs do not retransmit")

        for diagnostic in ["rssi", "heap", "uptime", "wifi_reconnects", "mqtt_reconnects"]:
            topic = "homeassistant/sensor/irbridge_aabbccddeeff/diag_" + diagnostic + "/config"
            sensor = json.loads(observer.message(topic)[2])
            assert sensor["entity_category"] == "diagnostic" and sensor["state_topic"] == BASE + "/info"
            assert sensor["device"]["identifiers"] == original["device"]["identifiers"]
        passed("Five production diagnostic discovery configs share the bridge device")

        since = time.monotonic()
        observer.publish("learn", {"action": "start", "confirmations": 3})
        observer.message(BASE + "/learn/status", lambda p: json.loads(p)["status"] == "waiting", since)
        since = time.monotonic()
        observer.publish("learn", {"action": "cancel"})
        observer.message(BASE + "/learn/status", lambda p: json.loads(p)["status"] == "idle", since)
        passed("MQTT learning start/cancel with actual shared LearningSession")

        since = time.monotonic()
        host.command("receive")
        received = json.loads(observer.message(BASE + "/received", since=since)[2])
        assert received["protocol"] == "NEC" and received["raw_length"] == 67 and "raw" not in received
        passed("Bounded MQTT received telemetry preserves signal metadata")

        since = time.monotonic()
        host.stop(abrupt=True)
        observer.message(BASE + "/status", lambda p: p == b"offline", since)
        host.start()
        host.online()
        passed("Broker publishes retained Last Will after abrupt native client termination")

        since = time.monotonic()
        host.command("rename")
        renamed = json.loads(observer.message(DISCOVERY, lambda p: p and json.loads(p)["name"] == "Ventilator An/Aus", since)[2])
        assert renamed["unique_id"] == original["unique_id"]
        assert renamed["payload_press"] == original["payload_press"]
        passed("Production DiscoveryManager updates a renamed entity without changing its ID")

        since = time.monotonic()
        host.command("wifi0")
        observer.message(BASE + "/status", lambda p: p == b"offline", since)
        assert not host.status()["connected"]
        host.command("wifi1")
        host.online()
        assert host.status()["reconnect_count"] >= 1
        passed("Production MQTT manager disconnects/reconnects after simulated WiFi link loss")

        broker.stop()
        wait_until(lambda: not host.status()["connected"], description="broker loss noticed")
        host.command("delete")
        host.command("learn")
        assert host.status()["learn"] == "waiting"
        broker.start()
        assert observer.ready.wait(8), "Observer did not reconnect"
        # Wait past the actual firmware MQTT retry interval while learning is active.
        time.sleep(5.5)
        assert not host.status()["connected"], "Reconnect interfered with active learning"
        since = time.monotonic()
        host.command("cancel")
        host.online()
        observer.message(DISCOVERY, lambda p: p == b"", since)
        assert host.status()["button_count"] == 0
        assert DISCOVERY in json.loads((directory / "discovery.json").read_text())
        probe = Observer(port)
        try:
            probe.message(BASE + "/status", lambda p: p == b"online")
            time.sleep(0.5)
            assert not probe.matching(DISCOVERY), "Deleted HA config still retained"
        finally:
            probe.close()
        passed("Real broker restart, learning defers reconnect, persistent offline HA deletion clears retained config")

        line = f"{len(results)}/{len(results)} MQTT TCP integration tests passed"
        results.append(line)
        print(line, flush=True)
    except Exception as exc:
        results.append("FAIL " + str(exc))
        print(results[-1], flush=True)
        raise
    finally:
        host.stop()
        if observer:
            observer.close()
        broker.stop()
        (OUT / "mqtt-tcp.log").write_text("\n".join(results) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
