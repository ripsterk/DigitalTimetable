from __future__ import annotations

import json
import os
from datetime import datetime
from pathlib import Path

from flask import Flask, jsonify, redirect, render_template, request, url_for
from werkzeug.utils import secure_filename

from parser import active_entry, entries_to_json, load_entries, next_entry, parse_any_excel, save_entries, room_key
from ai_cleanup import improve_entries_with_ai

BASE = Path(__file__).resolve().parent
DATA = BASE / "data"
UPLOADS = BASE / "uploads"
SCHEDULE_JSON = DATA / "schedule.json"
CONFIG_JSON = DATA / "config.json"

DATA.mkdir(exist_ok=True)
UPLOADS.mkdir(exist_ok=True)

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 16 * 1024 * 1024


def load_config():
    if CONFIG_JSON.exists():
        return json.loads(CONFIG_JSON.read_text(encoding="utf-8"))
    return {"selected_room": "EC 002"}


def save_config(cfg):
    CONFIG_JSON.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def not_found_payload(room: str):
    return {
        "found": False,
        "sala": room,
        "curs": "N/A",
        "profesor": "N/A",
        "ora": "N/A",
        "zi": "N/A",
    }


@app.route("/")
def index():
    entries = load_entries(SCHEDULE_JSON)
    cfg = load_config()
    rooms = sorted({e["room"] for e in entries if e.get("room") and e.get("room") != "N/A"})
    return render_template("index.html", entries=entries, rooms=rooms, cfg=cfg, ai_enabled=bool(os.environ.get("OPENAI_API_KEY")))


@app.route("/upload", methods=["POST"])
def upload():
    file = request.files.get("file")
    if not file or not file.filename:
        return redirect(url_for("index"))
    filename = secure_filename(file.filename)
    path = UPLOADS / filename
    file.save(path)
    entries = parse_any_excel(path)
    save_entries(entries, SCHEDULE_JSON)
    return redirect(url_for("index", uploaded=len(entries)))


@app.route("/manual", methods=["POST"])
def manual_add():
    entries = load_entries(SCHEDULE_JSON)
    room = request.form.get("room", "N/A").strip() or "N/A"
    day_index = int(request.form.get("day_index", 0))
    days = ["LUNI", "MARȚI", "MIERCURI", "JOI", "VINERI", "SÂMBĂTĂ", "DUMINICĂ"]
    entry = {
        "id": f"manual-{len(entries)+1}",
        "program": "Manual",
        "sheet": "Manual",
        "day": days[day_index],
        "day_index": day_index,
        "start": request.form.get("start", "08:00"),
        "end": request.form.get("end", "09:00"),
        "room": room,
        "room_key": room.replace(" ", "").upper(),
        "course": request.form.get("course", "N/A").strip() or "N/A",
        "professor": request.form.get("professor", "N/A").strip() or "N/A",
        "activity_type": request.form.get("activity_type", "N/A").strip() or "N/A",
        "group": request.form.get("group", "manual").strip() or "manual",
        "source_text": "manual dashboard entry",
        "source_cell": "manual",
    }
    entries.append(entry)
    SCHEDULE_JSON.write_text(json.dumps(entries, ensure_ascii=False, indent=2), encoding="utf-8")
    return redirect(url_for("index"))


@app.route("/select-room", methods=["POST"])
def select_room():
    cfg = load_config()
    cfg["selected_room"] = request.form.get("selected_room", "EC 002").strip() or "EC 002"
    save_config(cfg)
    return redirect(url_for("index"))


@app.route("/ai-cleanup", methods=["POST"])
def ai_cleanup():
    entries = load_entries(SCHEDULE_JSON)
    result = improve_entries_with_ai(entries)
    if result.get("entries"):
        SCHEDULE_JSON.write_text(json.dumps(result["entries"], ensure_ascii=False, indent=2), encoding="utf-8")
    return jsonify({"message": result.get("message"), "enabled": result.get("enabled")})




def is_na(value) -> bool:
    s = str(value or "").strip()
    return not s or s.upper() in {"N/A", "NA", "NULL", "NONE", "-"}


def active_candidates(entries, room: str, when):
    when = when or datetime.now()
    key = room_key(room)
    day_idx = when.weekday()
    t = when.strftime("%H:%M")
    return [
        e for e in entries
        if e.get("room_key") == key
        and e.get("day_index") == day_idx
        and e.get("start") <= t < e.get("end")
    ]


def enrich_active_entry(entry: dict, candidates: list[dict]) -> dict:
    if not entry:
        return entry
    enriched = dict(entry)
    if is_na(enriched.get("professor")):
        prof_candidates = [e for e in candidates if not is_na(e.get("professor"))]
        prof_candidates.sort(
            key=lambda e: (
                1 if not is_na(e.get("course")) else 0,
                1 if str(e.get("activity_type", "")).lower() == "curs" else 0,
                len(str(e.get("professor", ""))),
            ),
            reverse=True,
        )
        if prof_candidates:
            enriched["professor"] = prof_candidates[0].get("professor", "N/A")
    if is_na(enriched.get("course")):
        course_candidates = [e for e in candidates if not is_na(e.get("course"))]
        course_candidates.sort(
            key=lambda e: (
                1 if not is_na(e.get("professor")) else 0,
                1 if str(e.get("activity_type", "")).lower() == "curs" else 0,
                len(str(e.get("course", ""))),
            ),
            reverse=True,
        )
        if course_candidates:
            enriched["course"] = course_candidates[0].get("course", "N/A")
            enriched["activity_type"] = course_candidates[0].get("activity_type", enriched.get("activity_type", "N/A"))
            enriched["group"] = course_candidates[0].get("group", enriched.get("group", "N/A"))
    return enriched


@app.route("/api/current")
def api_current():
    entries = load_entries(SCHEDULE_JSON)
    room = request.args.get("sala") or load_config().get("selected_room", "EC 002")
    now_param = request.args.get("now")
    when = None
    if now_param:
        when = datetime.fromisoformat(now_param)

    candidates = active_candidates(entries, room, when)
    active = active_entry(entries, room, when)
    if not active:
        nxt = next_entry(entries, room, when)
        payload = not_found_payload(room)
        if nxt:
            try:
                nxt_when = (when or datetime.now()).replace(hour=int(str(nxt.get("start", "00:00")).split(":")[0]), minute=0)
                nxt = enrich_active_entry(nxt, active_candidates(entries, room, nxt_when))
            except Exception:
                pass
            payload["next"] = {
                "curs": nxt.get("course", "N/A"),
                "ora": f"{nxt.get('start')} - {nxt.get('end')}",
                "profesor": nxt.get("professor", "N/A"),
                "professor": nxt.get("professor", "N/A"),
                "zi": nxt.get("day", "N/A"),
            }
        return jsonify(payload)

    active = enrich_active_entry(active, candidates)
    prof = active.get("professor", "N/A")
    return jsonify({
        "found": True,
        "sala": active.get("room", room),
        "curs": active.get("course", "N/A"),
        "profesor": prof,
        "professor": prof,
        "ora": f"{active.get('start')} - {active.get('end')}",
        "zi": active.get("day", "N/A"),
        "tip": active.get("activity_type", "N/A"),
        "group": active.get("group", "N/A"),
        "source_text": active.get("source_text", ""),
    })


@app.route("/api/selected")
def api_selected():
    return jsonify(load_config())


@app.route("/api/schedule")
def api_schedule():
    return jsonify(load_entries(SCHEDULE_JSON))


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", "5000")), debug=True)
