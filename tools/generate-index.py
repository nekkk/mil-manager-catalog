#!/usr/bin/env python3
import json
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "catalog-source" / "catalog-source.json"
DIST_PATHS = [
    ROOT / "dist" / "index.json",
]

REQUIRED_FIELDS = ("id", "section", "titleId", "name", "downloadUrl")


def normalize_entry(entry: dict, defaults: dict) -> dict:
    merged = dict(defaults)
    merged.update(entry)

    missing = [field for field in REQUIRED_FIELDS if not merged.get(field)]
    if missing:
        raise ValueError(f"Entrada '{entry.get('id', '<sem id>')}' sem campos obrigatorios: {', '.join(missing)}")

    merged["titleId"] = str(merged["titleId"]).upper()
    merged.setdefault("summary", "")
    merged.setdefault("author", defaults.get("author", "M.I.L."))
    merged.setdefault("detailsUrl", defaults.get("detailsUrl", "https://miltraducoes.com/"))
    merged.setdefault("language", defaults.get("language", "pt-BR"))
    merged.setdefault("featured", False)
    merged.setdefault("tags", [])

    compatibility = merged.get("compatibility") or {}
    if not isinstance(compatibility, dict):
        raise ValueError(f"Entrada '{merged['id']}' com compatibility invalido")
    merged["compatibility"] = compatibility

    return merged


def build_index() -> dict:
    source = json.loads(SOURCE_PATH.read_text(encoding="utf-8"))
    defaults = source.get("defaults") or {}
    entries = [normalize_entry(entry, defaults) for entry in source.get("entries", [])]
    entries.sort(key=lambda item: (item.get("section", ""), item.get("name", "").lower()))

    generated_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")

    return {
        "catalogName": source.get("catalogName", "MIL Traducoes"),
        "channel": source.get("channel", "stable"),
        "schemaVersion": source.get("schemaVersion", "1.0"),
        "catalogRevision": source.get("catalogRevision", generated_at.replace("-", ".").replace(":", ".").replace("T", ".").replace("Z", "")),
        "generatedAt": generated_at,
        "entries": entries,
    }


def write_output(index_data: dict) -> None:
    serialized = json.dumps(index_data, ensure_ascii=False, indent=2) + "\n"
    for path in DIST_PATHS:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(serialized, encoding="utf-8")


def main() -> int:
    index_data = build_index()
    write_output(index_data)
    print(f"Index gerado com {len(index_data['entries'])} entradas.")
    print(f"Revisao: {index_data['catalogRevision']}")
    print(f"Saida principal: {DIST_PATHS[0]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
