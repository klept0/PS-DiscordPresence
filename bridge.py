from __future__ import annotations
import json, time, urllib.request, re
from dataclasses import dataclass, asdict
from pathlib import Path

ART_CACHE: dict[str, dict[str, str]] = {}

APP_DIR = Path.home() / ".ps-discordpresence-bridge"
CONFIG = APP_DIR / "config.json"

@dataclass
class Config:
    ps5_host: str = "192.168.69.8"
    ps5_port: int = 9878
    discord_application_id: str = ""
    poll_seconds: int = 5

def load_config() -> Config:
    APP_DIR.mkdir(parents=True, exist_ok=True)
    if not CONFIG.exists():
        c = Config(); CONFIG.write_text(json.dumps(asdict(c), indent=2)+"\n"); return c
    return Config(**json.loads(CONFIG.read_text()))

def fetch_status(c: Config) -> dict:
    with urllib.request.urlopen(f"http://{c.ps5_host}:{c.ps5_port}/api/status", timeout=8) as response:
        return json.load(response)

def playstation_metadata(content_id: str) -> dict[str, str]:
    if content_id in ART_CACHE:
        return ART_CACHE[content_id]
    if not re.fullmatch(r"[A-Z0-9_-]{8,80}", content_id or ""):
        return {}
    try:
        page = urllib.request.urlopen(f"https://store.playstation.com/en-us/product/{content_id}", timeout=12).read().decode("utf-8", "replace")
        match = re.search(r'<script[^>]+application/ld\+json[^>]*>(.*?)</script>', page, re.I | re.S)
        data = json.loads(match.group(1)) if match else {}
        name, image = data.get("name", ""), data.get("image", "")
        if isinstance(name, str) and isinstance(image, str) and image.startswith("https://image.api.playstation.com/"):
            ART_CACHE[content_id] = {"name": name, "image": image}
            return ART_CACHE[content_id]
    except Exception:
        pass
    return {}

def activity(status: dict) -> dict | None:
    state = status.get("state")
    content_id = status.get("contentid", "")
    store = playstation_metadata(content_id)
    title = status.get("titleName") or store.get("name") or status.get("titleid") or "PlayStation 5"
    firmware = status.get("firmware") or "unknown"
    platform = f"Playing on PlayStation 5 FW {firmware}"
    image = store.get("image") or (status.get("titleid") or "ps5").lower()
    if state == "active_running":
        return {"details": title, "state": platform, "large_image": image, "large_text": title}
    if state in {"suspended_or_home", "guide_menu"}:
        return {"details": title, "state": platform, "large_image": image, "large_text": title}
    return None

def main() -> None:
    c = load_config()
    if not c.discord_application_id.isdecimal():
        raise SystemExit(f"Set numeric discord_application_id in {CONFIG}")
    from pypresence import Presence
    from pypresence.exceptions import DiscordNotFound
    rpc = Presence(c.discord_application_id)
    try:
        rpc.connect()
    except DiscordNotFound:
        print("Bridge startup error: Discord desktop is not installed or running on this machine.")
        return
    last = object(); started = int(time.time())
    print(f"Bridge connected. Polling PS5 at {c.ps5_host}:{c.ps5_port}")
    while True:
        try:
            current = activity(fetch_status(c))
            marker = json.dumps(current, sort_keys=True)
            if marker != last:
                if current is None: rpc.clear(); print("Discord presence cleared")
                else: rpc.update(**current, start=started); print("Discord presence updated:", current["details"])
                last = marker
        except Exception as error:
            print("Bridge status:", error)
        time.sleep(max(3, c.poll_seconds))

if __name__ == "__main__": main()
