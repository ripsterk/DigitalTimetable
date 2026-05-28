"""Optional AI layer for bonus points.

The app works deterministically without AI. If OPENAI_API_KEY is set and the
openai package is installed, this module can review parser output and suggest
cleaned course/professor names. Keep AI outside the ESP32: the microcontroller
only consumes clean JSON.
"""
from __future__ import annotations

import json
import os
from typing import Any, Dict, List


def ai_available() -> bool:
    return bool(os.environ.get("OPENAI_API_KEY"))


def improve_entries_with_ai(entries: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Return an AI-cleaned copy of entries, or a clear disabled response.

    This function intentionally does not run by default during upload. The dashboard
    exposes it as an explicit AI action so you can demonstrate AI integration while
    keeping the base parser predictable.
    """
    if not ai_available():
        return {
            "enabled": False,
            "message": "AI cleanup is disabled. Set OPENAI_API_KEY to enable it.",
            "entries": entries,
        }

    try:
        from openai import OpenAI
    except Exception as exc:
        return {
            "enabled": False,
            "message": f"OPENAI_API_KEY is set, but the openai package is missing: {exc}",
            "entries": entries,
        }

    client = OpenAI()
    sample = entries[:120]
    schema = {
        "type": "object",
        "properties": {
            "entries": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "string"},
                        "course": {"type": "string"},
                        "professor": {"type": "string"},
                        "room": {"type": "string"},
                        "activity_type": {"type": "string"},
                    },
                    "required": ["id", "course", "professor", "room", "activity_type"],
                    "additionalProperties": False,
                },
            }
        },
        "required": ["entries"],
        "additionalProperties": False,
    }

    prompt = (
        "Clean Romanian university schedule entries. Do not invent data. "
        "Only normalize names, room spacing, and activity_type. Return only entries whose values you changed.\n\n"
        + json.dumps(sample, ensure_ascii=False)
    )

    try:
        response = client.responses.create(
            model=os.environ.get("OPENAI_MODEL", "gpt-4.1-mini"),
            input=[{"role": "user", "content": prompt}],
            text={
                "format": {
                    "type": "json_schema",
                    "name": "cleaned_schedule_entries",
                    "schema": schema,
                    "strict": True,
                }
            },
        )
        data = json.loads(response.output_text)
        changes = {item["id"]: item for item in data.get("entries", [])}
        merged = []
        for e in entries:
            if e.get("id") in changes:
                new_e = dict(e)
                new_e.update({k: v for k, v in changes[e["id"]].items() if k != "id"})
                merged.append(new_e)
            else:
                merged.append(e)
        return {
            "enabled": True,
            "message": f"AI reviewed {len(sample)} entries and suggested {len(changes)} changes.",
            "entries": merged,
        }
    except Exception as exc:
        return {
            "enabled": False,
            "message": f"AI cleanup failed: {exc}",
            "entries": entries,
        }
