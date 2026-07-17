# PS-DiscordPresence

PS5 game activity service with a local status endpoint and a Python Discord RPC bridge.

## Contents

- `src/` payload sources
- `bridge.py` desktop Discord RPC bridge

## Bridge

Install Python 3 and pypresence:

```text
python -m pip install pypresence
```

Run `bridge.py` once, then set `discord_application_id` in:

```text
~/.ps-discordpresence-bridge/config.json
```

The bridge reads `http://<ps5-host>:9878/api/status` and publishes the active title, official PlayStation Store cover art, and firmware line through the local Discord desktop client.

## Release

`v0.01` includes `PS-DiscordPresence.elf`.
