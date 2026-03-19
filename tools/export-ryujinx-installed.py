#!/usr/bin/env python3
import json
import os
from datetime import datetime, timezone
from pathlib import Path


APPDATA = Path(os.environ.get("APPDATA", ""))
RYUJINX_ROOT = APPDATA / "Ryujinx"
GAMES_DIR = RYUJINX_ROOT / "games"
OUTPUT_PATH = RYUJINX_ROOT / "sdcard" / "switch" / "mil_manager" / "emulator-installed.json"


def detect_version(title_dir: Path) -> str:
    cpu_cache_dir = title_dir / "cache" / "cpu"
    versions = []
    if cpu_cache_dir.exists():
        for cache_file in cpu_cache_dir.rglob("*.cache"):
            name = cache_file.stem
            version = name.split("-", 1)[0]
            if version and version.lower() != "default":
                versions.append(version)
    versions = sorted(set(versions))
    return versions[-1] if versions else ""


def main() -> int:
    titles = []
    if GAMES_DIR.exists():
        for child in sorted(GAMES_DIR.iterdir()):
            if not child.is_dir():
                continue
            title_id = child.name.upper()
            if len(title_id) != 16:
                continue
            try:
                int(title_id, 16)
            except ValueError:
                continue
            titles.append(
                {
                    "titleId": title_id,
                    "displayVersion": detect_version(child),
                }
            )

    payload = {
        "generatedAt": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "source": str(GAMES_DIR),
        "titles": titles,
    }

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Exportados {len(titles)} titulos para {OUTPUT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
