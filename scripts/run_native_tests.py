"""Compile and execute shared firmware code plus real IRremoteESP8266 algorithms."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which("g++")
if not compiler:
    candidate = ROOT / ".tools/native-core/packages/toolchain-gccmingw32/bin/g++.exe"
    if candidate.exists():
        compiler = str(candidate)
if not compiler:
    sys.exit("g++ fehlt. Installiere GCC oder: PLATFORMIO_CORE_DIR=.tools/native-core pio pkg install -g -t platformio/toolchain-gccmingw32")
json = ROOT / ".pio/libdeps/esp_ir_usb/ArduinoJson/src"
ir = ROOT / ".pio/libdeps/esp_ir_usb/IRremoteESP8266"
if not json.exists():
    sys.exit("Zuerst pio run -e esp_ir_usb ausführen.")
out = ROOT / "test/results"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("native_tests.exe" if os.name == "nt" else "native_tests")
sources = [*sorted((ROOT / "lib/BridgeCore/src").glob("*.cpp")), ROOT / "test/native_tests.cpp"]
command = [compiler, "-std=c++1z", "-O1", "-Wall", "-Wextra", "-Werror",
           "-I" + str(json), "-I" + str(ROOT / "lib/BridgeCore/src"),
           *map(str, sources), "-o", str(exe)]
env = dict(os.environ)
env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
subprocess.run(command, cwd=ROOT, env=env, check=True)
result = subprocess.run([str(exe)], cwd=ROOT, env=env, capture_output=True, text=True)
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
(out / "native.log").write_text(result.stdout + result.stderr, encoding="utf-8")
if result.returncode:
    sys.exit(result.returncode)
ir_exe = out / ("ir_tests.exe" if os.name == "nt" else "ir_tests")
ir_sources = [ROOT / "test/ir_library_tests.cpp", ROOT / "lib/IRCodec/src/ir_codec.cpp",
              *sorted((ROOT / "lib/BridgeCore/src").glob("*.cpp")),
              *[ir / "src" / name for name in ("IRrecv.cpp","IRsend.cpp","IRutils.cpp","IRtext.cpp","IRtimer.cpp","ir_NEC.cpp","ir_Daikin.cpp")]]
ir_command = [compiler,"-std=c++1z","-O1","-DUNIT_TEST=1","-D_IR_ENABLE_DEFAULT_=false",
              "-DDECODE_NEC=true","-DSEND_NEC=true","-DSEND_RAW=true","-DDECODE_HASH=true",
              "-DDECODE_DAIKIN=true","-DSEND_DAIKIN=true",
              "-I"+str(json),"-I"+str(ROOT / "lib/BridgeCore/src"),"-I"+str(ROOT / "lib/IRCodec/src"),
              "-I"+str(ir / "src"),"-I"+str(ir / "test"),*map(str,ir_sources),"-o",str(ir_exe)]
subprocess.run(ir_command,cwd=ROOT,env=env,check=True)
result = subprocess.run([str(ir_exe)],cwd=ROOT,env=env,capture_output=True,text=True)
print(result.stdout,end=""); print(result.stderr,end="",file=sys.stderr)
(out / "ir-library.log").write_text(result.stdout+result.stderr,encoding="utf-8")
sys.exit(result.returncode)
