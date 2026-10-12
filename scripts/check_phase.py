"""Run the real PlatformIO build and append its successful result to the journal."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("phase", type=int)
parser.add_argument("description")
args = parser.parse_args()
pio = shutil.which("pio") or shutil.which("platformio")
if not pio:
    candidate = Path.home() / ".platformio/penv/Scripts/platformio.exe"
    if candidate.exists():
        pio = str(candidate)
if not pio:
    sys.exit("PlatformIO fehlt: python -m pip install platformio")
env = dict(os.environ, PLATFORMIO_CORE_DIR=str(ROOT / ".pio-core"),
           PLATFORMIO_SETTING_ENABLE_TELEMETRY="no")
logs = ROOT / "artifacts"
logs.mkdir(exist_ok=True)
with (logs / f"phase-{args.phase:02d}.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([pio, "run", "-e", "esp_ir_usb"], cwd=ROOT,
                               env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace")
    for line in process.stdout:
        print(line, end="", flush=True)
        log.write(line)
    code = process.wait()
if code:
    sys.exit(code)
with (ROOT / "docs/DEVELOPMENT.md").open("a", encoding="utf-8") as journal:
    journal.write(f"\n- Phase {args.phase}: {args.description}. "
                  "`pio run -e esp_ir_usb`: **SUCCESS**.\n")
