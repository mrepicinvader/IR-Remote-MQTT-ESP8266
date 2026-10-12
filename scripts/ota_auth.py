"""Pass optional OTA auth from the environment; never put it in platformio.ini."""
import os
Import("env")
password = os.environ.get("IRBRIDGE_OTA_PASSWORD")
if password:
    env.Append(UPLOAD_FLAGS=["--auth=" + password])
