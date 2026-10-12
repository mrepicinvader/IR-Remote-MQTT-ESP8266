"""Run final clean USB firmware, LittleFS image and OTA firmware build checks.

No upload or device modification is performed. Logs stay under artifacts/.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = shutil.which("pio") or shutil.which("platformio")
if not pio and os.name == "nt":
    candidate = Path.home() / ".platformio/penv/Scripts/platformio.exe"
    if candidate.exists():
        pio = str(candidate)
if not pio:
    sys.exit("PlatformIO fehlt. Installiere PlatformIO Core oder die VS-Code-Erweiterung.")
env = dict(os.environ)
env.setdefault("PLATFORMIO_CORE_DIR", str(ROOT / ".pio-core"))
out = ROOT / "artifacts"
out.mkdir(exist_ok=True)
checks = [
    ("clean", ["run", "-e", "esp_ir_usb", "-t", "clean"]),
    ("clean-firmware", ["run"]),
    ("littlefs", ["run", "-e", "esp_ir_usb", "-t", "buildfs"]),
    ("ota-firmware", ["run", "-e", "esp_ir_ota"]),
]
for name, args in checks:
    print(f"START {name}: pio {' '.join(args)}", flush=True)
    with (out / f"{name}.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([pio, *args], cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            log.write(line)
            if any(word in line for word in ("error:", "Error", "Warning!", "RAM:", "Flash:", "SUCCESS", "FAILED", "Installing", "Downloading", "Building FS")):
                print(line.rstrip(), flush=True)
        process.wait()
    if process.returncode:
        sys.exit(f"FAIL {name}; siehe artifacts/{name}.log")
    print(f"PASS {name}", flush=True)
print("4/4 PlatformIO build checks passed", flush=True)
