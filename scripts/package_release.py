"""Package sources, verified build images and check logs; no credentials/caches."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts"
version = re.search(r'VERSION\[\]\s*=\s*"(\d+\.\d+\.\d+)"', (ROOT / "include/config.h").read_text()).group(1)
prefix = "esp-ir-bridge-" + version
OUT.mkdir(exist_ok=True)
sources = [ROOT / "platformio.ini", ROOT / ".gitignore", ROOT / "README.md"]
for folder in ["src", "include", "lib", "data", "scripts", "test", "docs"]:
    for directory, subdirectories, names in os.walk(ROOT / folder):
        subdirectories[:] = [name for name in subdirectories if name not in {"__pycache__", "results", "runtime"}]
        for name in names:
            path = Path(directory) / name
            if name in {"secrets.h", "config.json", ".env", "env_secrets.ini"} or path.suffix.lower() in {".bin", ".exe", ".elf", ".map", ".pyc"}:
                continue
            sources.append(path)
sources = sorted(sources)
images = {
    "images/firmware-usb.bin": ROOT / ".pio/build/esp_ir_usb/firmware.bin",
    "images/firmware-ota.bin": ROOT / ".pio/build/esp_ir_ota/firmware.bin",
    "images/littlefs.bin": ROOT / ".pio/build/esp_ir_usb/littlefs.bin",
}
logs = {
    "checks/clean.log": OUT / "clean.log",
    "checks/clean-firmware.log": OUT / "clean-firmware.log",
    "checks/littlefs.log": OUT / "littlefs.log",
    "checks/ota-firmware.log": OUT / "ota-firmware.log",
    "checks/native.log": ROOT / "test/results/native.log",
    "checks/ir-library.log": ROOT / "test/results/ir-library.log",
    "checks/mqtt-tcp.log": ROOT / "test/results/mqtt-tcp.log",
    "checks/web-assets.log": ROOT / "test/results/web-assets.log",
    "checks/web-browser.log": ROOT / "test/results/web-browser.log",
}
for path in [*images.values(), *logs.values()]:
    if not path.is_file():
        raise SystemExit(f"Missing build/check artifact: {path.relative_to(ROOT)}")
for path in logs.values():
    text = path.read_text(encoding="utf-8")
    if "FAIL" in text or ("SUCCESS" not in text and "PASS" not in text):
        raise SystemExit(f"Unsuccessful check: {path.relative_to(ROOT)}")

firmware_inputs = [ROOT / "platformio.ini", *[path for path in sources if path.relative_to(ROOT).parts[0] in {"include", "src", "lib"}]]
for name in ["images/firmware-usb.bin", "images/firmware-ota.bin"]:
    if max(path.stat().st_mtime for path in firmware_inputs) > images[name].stat().st_mtime:
        raise SystemExit("Firmware is older than its sources; run verify_builds.py")
if max(path.stat().st_mtime for path in sources if path.relative_to(ROOT).parts[0] == "data") > images["images/littlefs.bin"].stat().st_mtime:
    raise SystemExit("LittleFS image is older than its assets; run buildfs")

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

manifest = {
    "firmware_version": version,
    "created_utc": datetime.now(timezone.utc).isoformat(),
    "board": "ESP_IR_TR_WIFI / ESP-12F",
    "flash_layout": "4 MB flash, 1 MB LittleFS",
    "hardware_verified": False,
    "source_file_count": len(sources),
    "images": [{"archive_path": name, "size_bytes": path.stat().st_size, "sha256": digest(path)} for name, path in images.items()],
    "checks": [{"archive_path": name, "sha256": digest(path)} for name, path in logs.items()],
}
source_archive = OUT / (prefix + "-source.zip")
complete_archive = OUT / (prefix + ".zip")
with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sources:
        archive.write(path, prefix + "/" + path.relative_to(ROOT).as_posix())
with zipfile.ZipFile(complete_archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sources:
        archive.write(path, prefix + "/" + path.relative_to(ROOT).as_posix())
    for name, path in {**images, **logs}.items():
        archive.write(path, prefix + "/" + name)
    archive.writestr(prefix + "/manifest.json", json.dumps(manifest, indent=2) + "\n")
for path in [source_archive, complete_archive]:
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise SystemExit("Archive CRC verification failed")
manifest["archives"] = [{"path": path.name, "size_bytes": path.stat().st_size, "sha256": digest(path)} for path in [source_archive, complete_archive]]
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"PASS {len(sources)} source files, 3 build images and {len(logs)} check logs packaged; archive CRCs verified")
print(complete_archive)
