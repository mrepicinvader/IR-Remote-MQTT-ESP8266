"""Static web/documentation consistency plus JavaScript syntax; no browser emulation."""
from pathlib import Path
from html.parser import HTMLParser
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids, self.forms, self.assets = [], {}, []
        self.form, self.submit = None, 0

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if "id" in attributes:
            self.ids.append(attributes["id"])
        if tag == "form":
            self.form, self.submit = attributes.get("id"), 0
        if tag == "button" and attributes.get("type") == "submit" and self.form:
            self.submit += 1
        if tag == "script" and "src" in attributes:
            self.assets.append(attributes["src"])
        if tag == "link" and "href" in attributes:
            self.assets.append(attributes["href"])

    def handle_endtag(self, tag):
        if tag == "form":
            self.forms[self.form], self.form = self.submit, None

page = Page()
page.feed((ROOT / "data/index.html").read_text(encoding="utf-8"))
assert len(page.ids) == len(set(page.ids)), "Duplicate HTML IDs"
assert all(page.forms.values()), "Form without an explicit submit button"
assert all(url.startswith("/") and (ROOT / "data" / url[1:]).is_file() for url in page.assets), "Non-local or missing asset"
script = (ROOT / "data/app.js").read_text(encoding="utf-8")
for selector in re.findall(r"\$\('#([A-Za-z0-9_-]+)'", script):
    assert selector in page.ids, "Missing HTML ID: " + selector
report = [f"PASS static web assets: {len(page.ids)} unique IDs, {len(page.forms)} forms, local assets and literal JS selectors"]

readme = (ROOT / "README.md").read_text(encoding="utf-8")
fence = chr(96) * 3
for block in re.findall(fence + r"json\n(.*?)\n" + fence, readme, re.S):
    example = json.loads(block)
    if "version" in example:
        assert "configuration" in example
        for remote in example["remotes"]:
            for button in remote["buttons"]:
                signal = button["signal"]
                assert {"protocol", "bits", "value", "address", "command", "frequency", "raw_length", "raw"} <= signal.keys()
                assert signal["raw_length"] == len(signal["raw"])
report.append("PASS README examples: valid JSON and required signal metadata")
node = shutil.which("node")
if not node and os.name == "nt":
    candidate = Path.home() / "AppData/Local/Programs/cursor/resources/app/resources/helpers/node.exe"
    if candidate.exists():
        node = str(candidate)
if node:
    for asset in page.assets:
        if asset.endswith(".js"):
            subprocess.run([node, "--check", str(ROOT / "data" / asset[1:])], check=True)
    report.append("PASS all JavaScript asset syntax")
    verify_md5 = """const fs=require('fs'),vm=require('vm'),crypto=require('crypto');
vm.runInThisContext(fs.readFileSync('data/md5.js','utf8'));
for (const length of [0,1,3,55,56,63,64,65,127,128,1024,1044464]) {
  const bytes=Uint8Array.from({length},(_,i)=>(i*17+31)%256);
  const expected=crypto.createHash('md5').update(bytes).digest('hex');
  if (firmwareMd5(bytes)!==expected) throw new Error('MD5 mismatch at length '+length);
}
"""
    subprocess.run([node,"-e",verify_md5],cwd=ROOT,check=True)
    report.append("PASS firmware transfer MD5 against Node crypto, including padding boundaries")
else:
    report.append("SKIP JavaScript syntax: node not found")
report.append("Static/syntax checks only; no interactive browser or ESP8266 HTTP execution.")
output = ROOT / "test/results"
output.mkdir(parents=True, exist_ok=True)
(output / "web-assets.log").write_text("\n".join(report) + "\n", encoding="utf-8")
print("\n".join(report))
