"""Isolated TCP test broker, bound only to localhost. No external broker access."""
from pathlib import Path
import asyncio
import logging
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/mqtt-python"))
from amqtt.broker import Broker

async def main():
    port = int(sys.argv[1])
    broker = Broker({
        "listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    })
    await broker.start()
    print("READY", flush=True)
    try:
        await asyncio.Future()
    finally:
        await broker.shutdown()

if __name__ == "__main__":
    logging.basicConfig(level=logging.WARNING)
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
