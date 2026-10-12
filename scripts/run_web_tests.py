"""Exercise local assets in an isolated headless Chromium through DevTools.

Uses the stateful UI fixture, not ESP8266 HTTP/IR/flash hardware. Never connects
to an existing browser profile. Requires websockets from mqtt-test-requirements.
"""
import asyncio
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import uuid
import time
from urllib.request import urlopen, Request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT / ".tools/mqtt-python"))
from websockets.asyncio.client import connect

OUT = ROOT / "artifacts"
RESULTS = ROOT / "test/results"

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        return sock.getsockname()[1]

def browser_executable():
    candidates = [os.environ.get("CHROME_PATH"),shutil.which("chromium"),shutil.which("google-chrome"),
        "C:/Program Files/Google/Chrome/Application/chrome.exe","C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"]
    return next((str(path) for path in candidates if path and Path(path).is_file()),None)

def wait_http(url,timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            with urlopen(url,timeout=1) as response: return response.read()
        except Exception: time.sleep(0.1)
    raise AssertionError("Local service did not start: "+url)

class Page:
    def __init__(self,ws):
        self.ws=ws; self.sequence=0; self.errors=[]

    async def call(self,method,params=None):
        self.sequence+=1
        await self.ws.send(json.dumps(dict(id=self.sequence,method=method,params=params or {})))
        while True:
            message=json.loads(await self.ws.recv())
            if message.get("method")=="Runtime.exceptionThrown": self.errors.append(message["params"]["exceptionDetails"])
            if message.get("id")==self.sequence:
                if "error" in message: raise AssertionError(message["error"])
                return message.get("result",{})

    async def evaluate(self,expression):
        result=await self.call("Runtime.evaluate",dict(expression=expression,awaitPromise=True,returnByValue=True,userGesture=True))
        if "exceptionDetails" in result: raise AssertionError(result["exceptionDetails"])
        return result.get("result",{}).get("value")

    async def wait(self,expression,timeout=15):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if await self.evaluate(expression): return
            await asyncio.sleep(0.1)
        raise AssertionError("UI condition timed out: "+expression)

    async def go(self,url):
        await self.call("Page.navigate",dict(url=url))
        await self.wait("document.readyState==='complete' && document.querySelector('#connection')?.textContent==='Online'")

    async def click(self,selector):
        await self.evaluate("document.querySelector("+json.dumps(selector)+").click()")

    async def screenshot(self,name):
        result=await self.call("Page.captureScreenshot",dict(format="png",captureBeyondViewport=True))
        (OUT / name).write_bytes(base64.b64decode(result["data"]))

    async def file(self,path):
        root=(await self.call("DOM.getDocument"))["root"]["nodeId"]
        node=(await self.call("DOM.querySelector",dict(nodeId=root,selector="#firmware-form input[type=file]")))["nodeId"]
        await self.call("DOM.setFileInputFiles",dict(nodeId=node,files=[str(path.resolve())]))

async def exercise(websocket_url,base,passed):
    async with connect(websocket_url,max_size=8*1024*1024) as ws:
        page=Page(ws)
        await page.call("Page.enable"); await page.call("Runtime.enable")
        await page.call("Emulation.setDeviceMetricsOverride",dict(width=1280,height=900,deviceScaleFactor=1,mobile=False))
        await page.go(base)
        await page.wait("document.querySelectorAll('.key').length===2")
        await page.screenshot("ui-desktop.png")
        passed("Home screen renders local assets without JavaScript exceptions")

        await page.click(".key .menu summary")
        await page.evaluate("[...document.querySelector('.key .menu-panel').children].find(b=>b.textContent==='Bearbeiten').click()")
        await page.wait("document.querySelector('#details-dialog').open")
        await page.evaluate("const f=document.querySelector('#details-form'); f.elements.icon.value='tv'; f.elements.favorite.checked=true")
        await page.click("#details-form button[type=submit]")
        await page.wait("!document.querySelector('#favorites-panel').hidden && !document.querySelector('#details-dialog').open")
        assert await page.evaluate("document.querySelector('.key-send').textContent.includes('▣')")
        await page.click(".key .menu summary")
        await page.evaluate("[...document.querySelector('.key .menu-panel').children].find(b=>b.textContent==='Nach hinten').click()")
        await page.wait("document.querySelector('.key-send').textContent.includes('Speed +')")
        passed("Icons, favorites and button ordering persist through UI edits")

        await page.click(".remote-header .menu summary")
        await page.evaluate("[...document.querySelector('.remote-header .menu-panel').children].find(b=>b.textContent==='Remote bearbeiten').click()")
        await page.evaluate("document.querySelector('#remote-form').elements.layout.value='directional'")
        await page.click("#remote-form button[type=submit]")
        await page.wait("document.querySelector('.key-grid.directional .slot-power')!==null")
        assert await page.evaluate("getComputedStyle(document.querySelector('.directional .slot-power')).gridRowStart==='1'")
        passed("Directional layout places assigned controls and retains extra buttons")

        await page.click("#add-scene")
        await page.evaluate("document.querySelector('#scene-form').elements.name.value='Testszene'")
        await page.click("#scene-add-step")
        await page.evaluate("document.querySelectorAll('.scene-button')[1].value='00000001/00000002'; document.querySelectorAll('.scene-delay')[1].value='100'")
        await page.click("#scene-form button[type=submit]")
        await page.wait("document.querySelector('#scenes').textContent.includes('Testszene') && !document.querySelector('#scene-dialog').open")
        await page.click("#scenes .scene-row>button")
        await page.wait("document.querySelector('#toast').textContent.includes('Testszene · Gesendet')")
        passed("Scene editor saves multiple steps and waits for a sent result")

        await page.click("#batch-learn")
        await page.evaluate("const b=document.querySelector('#batch-form'); b.elements.names.value="+json.dumps("Lauter\nLeiser")+"; b.elements.confirmations.value='1'")
        await page.click("#batch-form button[type=submit]")
        await page.wait("document.querySelector('#batch-progress').textContent.includes('Gespeichert') && !document.querySelector('#batch-test').disabled")
        await page.click("#batch-test")
        await page.wait("!document.querySelector('#batch-test').disabled")
        await page.click("#batch-next")
        await page.wait("document.querySelector('#batch-progress').textContent.includes('Leiser') && document.querySelector('#batch-progress').textContent.includes('Gespeichert')")
        await page.click("#batch-next")
        await page.wait("document.querySelector('#batch-progress').textContent.includes('abgeschlossen')")
        await page.click("#batch-cancel")
        await page.wait("document.querySelectorAll('.key').length===4")
        passed("Batch wizard learns, tests and advances through two saved buttons")

        await page.go(base+"diagnostics")
        await page.wait("document.querySelector('#command-history').textContent.includes('Gesendet')")
        passed("Diagnostics show correlated terminal command results")

        await page.go(base+"settings")
        await page.wait("document.querySelector('#config-form').elements.wifi_ssid.value==='Test network'")
        assert await page.evaluate("document.querySelector('#config-form').elements.ap_fallback_enabled.checked")
        bad=OUT / "invalid-firmware.bin"; bad.write_bytes(b'\x7fELF'+bytes(124))
        await page.file(bad); await page.click("#firmware-form button[type=submit]")
        await page.wait("document.querySelector('#toast').textContent.includes('keine passende')")
        await page.file(ROOT / ".pio/build/esp_ir_usb/firmware.bin")
        await page.click("#firmware-form button[type=submit]")
        await page.wait("document.querySelector('#firmware-message').textContent.includes('Firmware geprüft')")
        passed("Firmware UI rejects foreign files and transfers a real build with matching MD5")

        await page.go(base+"settings")
        await page.wait("document.querySelector('#config-form').elements.wifi_ssid.value==='Test network'")
        await page.evaluate("const c=document.querySelector('#config-form'); c.elements.ap_fallback_enabled.checked=false; c.elements.recovery_password.value='private-recovery-key'")
        await page.click("#config-form button[type=submit]")
        await page.wait("document.querySelector('#toast').textContent.includes('Gespeichert')")
        saved=json.loads(urlopen(base+"api/config").read())
        assert saved["ap_fallback_enabled"] is False and "recovery_password" not in saved
        passed("Recovery settings save fallback preference without exposing passwords")

        await page.go(base)
        await page.call("Emulation.setDeviceMetricsOverride",dict(width=390,height=844,deviceScaleFactor=1,mobile=True))
        await page.screenshot("ui-mobile.png")
        assert await page.evaluate("document.documentElement.scrollWidth<=window.innerWidth")
        await page.click("#add-scene"); await page.screenshot("ui-scene-mobile.png")
        assert await page.evaluate("document.querySelector('#scene-dialog').getBoundingClientRect().right<=window.innerWidth")
        passed("Mobile home and scene dialog fit a 390 px viewport")
        assert not page.errors, json.dumps(page.errors)

def main():
    OUT.mkdir(exist_ok=True); RESULTS.mkdir(exist_ok=True)
    executable=browser_executable()
    if not executable: sys.exit("Chromium not found; set CHROME_PATH")
    debug_port,fixture_port=free_port(),free_port()
    base=f"http://127.0.0.1:{fixture_port}/"
    profile=OUT / ("browser-qa-"+uuid.uuid4().hex[:8]); profile.mkdir()
    flags=dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name=="nt" else {}
    results=[]
    def passed(name):
        results.append("PASS "+name); print(results[-1],flush=True)
    with (OUT / "ui-browser.log").open("w",encoding="utf-8") as log:
        fixture=subprocess.Popen([sys.executable,str(ROOT / "scripts/ui_test_server.py"),"--port",str(fixture_port)],cwd=ROOT,stdout=log,stderr=log,**flags)
        chrome=subprocess.Popen([executable,"--headless=new","--disable-gpu","--no-first-run","--no-default-browser-check",
            "--disable-background-networking","--disable-component-update","--disable-sync","--metrics-recording-only",
            "--remote-debugging-address=127.0.0.1",f"--remote-debugging-port={debug_port}",f"--user-data-dir={profile}","about:blank"],stdout=log,stderr=log,**flags)
        try:
            wait_http(base+"api/status")
            wait_http(f"http://127.0.0.1:{debug_port}/json/version")
            request=Request(f"http://127.0.0.1:{debug_port}/json/new?about:blank",method="PUT")
            target=json.loads(urlopen(request).read())
            asyncio.run(exercise(target["webSocketDebuggerUrl"],base,passed))
            line=f"{len(results)}/{len(results)} browser fixture tests passed"; results.append(line); print(line,flush=True)
        except Exception as exc:
            results.append("FAIL "+str(exc)); raise
        finally:
            for process in (chrome,fixture):
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
            (RESULTS / "web-browser.log").write_text("\n".join(results)+"\n",encoding="utf-8")

if __name__=="__main__": main()
