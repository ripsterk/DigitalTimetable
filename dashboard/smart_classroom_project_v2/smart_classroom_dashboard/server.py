from __future__ import annotations

import html
import json
import os
import re
from datetime import datetime
from zoneinfo import ZoneInfo
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from parser import active_entry, load_entries, next_entry, parse_any_excel, save_entries, room_key
from ai_cleanup import improve_entries_with_ai

BASE = Path(__file__).resolve().parent
DATA = BASE / "data"
UPLOADS = BASE / "uploads"
SCHEDULE_JSON = DATA / "schedule.json"
CONFIG_JSON = DATA / "config.json"
DATA.mkdir(exist_ok=True)
UPLOADS.mkdir(exist_ok=True)
BUCHAREST_TZ = ZoneInfo("Europe/Bucharest")


def load_config():
    if CONFIG_JSON.exists():
        return json.loads(CONFIG_JSON.read_text(encoding="utf-8"))
    return {"selected_room": "EC 002"}


def save_config(cfg):
    CONFIG_JSON.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def normalize_room_key(room: str) -> str:
    return re.sub(r"\s+", "", room or "").upper()




def is_na(value) -> bool:
    s = str(value or "").strip()
    return not s or s.upper() in {"N/A", "NA", "NULL", "NONE", "-"}


def active_candidates(entries, room: str, when: datetime):
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
    """Fill missing professor/course from overlapping same-room rows."""
    if not entry:
        return entry

    enriched = dict(entry)

    if is_na(enriched.get("professor")):
        prof_candidates = [
            e for e in candidates
            if not is_na(e.get("professor"))
        ]
        # Prefer candidates that also have a real course, then curs rows.
        prof_candidates.sort(
            key=lambda e: (
                1 if not is_na(e.get("course")) else 0,
                1 if str(e.get("activity_type", "")).lower() == "curs" else 0,
                len(str(e.get("professor", "")))
            ),
            reverse=True,
        )
        if prof_candidates:
            enriched["professor"] = prof_candidates[0].get("professor", "N/A")
            enriched["source_text"] = (
                str(enriched.get("source_text", ""))
                + " | professor fallback: "
                + str(prof_candidates[0].get("source_text", ""))
            )

    if is_na(enriched.get("course")):
        course_candidates = [
            e for e in candidates
            if not is_na(e.get("course"))
        ]
        course_candidates.sort(
            key=lambda e: (
                1 if not is_na(e.get("professor")) else 0,
                1 if str(e.get("activity_type", "")).lower() == "curs" else 0,
                len(str(e.get("course", "")))
            ),
            reverse=True,
        )
        if course_candidates:
            enriched["course"] = course_candidates[0].get("course", "N/A")
            enriched["activity_type"] = course_candidates[0].get("activity_type", enriched.get("activity_type", "N/A"))
            enriched["group"] = course_candidates[0].get("group", enriched.get("group", "N/A"))

    return enriched


def course_acronym(name: str) -> str:
    """Small deterministic acronym fallback for TFT display."""
    if not name or name == "N/A":
        return "N/A"
    if len(name) <= 8:
        return name
    # Remove parenthesized translation/details.
    name = re.sub(r"\([^)]*\)", " ", name)
    stop = {"si", "și", "in", "în", "de", "la", "cu", "pe", "the", "and", "of", "for", "to", "a", "an"}
    letters = []
    for tok in re.split(r"[\s,./_\-]+", name):
        tok = re.sub(r"[^A-Za-z0-9ĂÂÎȘŞȚŢăâîșşțţ]", "", tok).strip()
        if not tok:
            continue
        low = tok.lower()
        if low in stop:
            continue
        letters.append(tok[0].upper())
    return "".join(letters)[:8] if letters else name[:8].upper()


def normalize_when(now_param: str | None = None) -> datetime:
    """Return Bucharest local naive datetime, so schedule comparisons are stable."""
    if now_param:
        when = datetime.fromisoformat(now_param)
        if when.tzinfo is not None:
            when = when.astimezone(BUCHAREST_TZ)
        return when.replace(tzinfo=None)
    return datetime.now(BUCHAREST_TZ).replace(tzinfo=None)


def not_found_payload(room: str, when: datetime):
    return {
        "found": False,
        "server_now": when.strftime("%Y-%m-%d %H:%M"),
        "sala": room,
        "curs": "N/A",
        "curs_scurt": "N/A",
        "profesor": "N/A",
        "ora": "N/A",
        "zi": "N/A",
    }


def entry_payload(entry: dict, room: str, when: datetime, found: bool = True):
    course = entry.get("course", "N/A")
    prof = entry.get("professor", "N/A") or "N/A"
    payload = {
        "found": found,
        "server_now": when.strftime("%Y-%m-%d %H:%M"),
        "sala": entry.get("room", room),
        "curs": course,
        "curs_scurt": entry.get("course_short") or entry.get("acronym") or course_acronym(course),
        "profesor": prof,
        "professor": prof,
        "ora": f"{entry.get('start')} - {entry.get('end')}",
        "zi": entry.get("day", "N/A"),
        "tip": entry.get("activity_type", "N/A"),
        "group": entry.get("group", "N/A"),
        "source_text": entry.get("source_text", ""),
    }
    return payload


def current_payload(room: str, now_param: str | None = None):
    entries = load_entries(SCHEDULE_JSON)
    when = normalize_when(now_param)

    candidates = active_candidates(entries, room, when)
    active = active_entry(entries, room, when)

    if active:
        active = enrich_active_entry(active, candidates)
        return entry_payload(active, room, when, True)

    payload = not_found_payload(room, when)
    nxt = next_entry(entries, room, when)
    if nxt:
        # Enrich next as well using rows at the start time of the next class.
        try:
            nxt_when = when.replace(hour=int(str(nxt.get("start", "00:00")).split(":")[0]), minute=0)
            nxt_candidates = active_candidates(entries, room, nxt_when)
            nxt = enrich_active_entry(nxt, nxt_candidates)
        except Exception:
            pass

        next_payload = entry_payload(nxt, room, when, False)
        # Keep only the compact next object inside current response.
        payload["next"] = {
            "curs": next_payload["curs"],
            "curs_scurt": next_payload["curs_scurt"],
            "ora": next_payload["ora"],
            "profesor": next_payload["profesor"],
            "professor": next_payload["professor"],
            "zi": next_payload["zi"],
            "tip": next_payload["tip"],
            "group": next_payload["group"],
        }
    return payload

def page_html(message: str = "") -> str:
    entries = load_entries(SCHEDULE_JSON)
    cfg = load_config()
    rooms = sorted({e["room"] for e in entries if e.get("room") and e.get("room") != "N/A"})
    selected_room = cfg.get("selected_room", "EC 002")
    options = "".join(
        f'<option value="{html.escape(r)}" {"selected" if r == selected_room else ""}>{html.escape(r)}</option>'
        for r in rooms
    )
    rows = "".join(
        "<tr>" +
        f"<td>{html.escape(str(e.get('day','')))}</td>" +
        f"<td>{html.escape(str(e.get('start','')))}–{html.escape(str(e.get('end','')))}</td>" +
        f"<td>{html.escape(str(e.get('room','')))}</td>" +
        f"<td>{html.escape(str(e.get('course','')))}</td>" +
        f"<td>{html.escape(str(e.get('professor','')))}</td>" +
        f"<td>{html.escape(str(e.get('activity_type','')))}</td>" +
        f"<td>{html.escape(str(e.get('group','')))}</td>" +
        f"<td>{html.escape(str(e.get('source_text','')))}</td>" +
        "</tr>"
        for e in entries[:500]
    )
    ai_status = "enabled" if os.environ.get("OPENAI_API_KEY") else "disabled until OPENAI_API_KEY is set"
    msg = f'<p class="msg">{html.escape(message)}</p>' if message else ""
    return f"""<!doctype html>
<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Smart Classroom Controller</title>
<style>
:root {{ --bg:#0f172a; --card:#111827; --muted:#94a3b8; --line:#334155; --accent:#0ea5e9; --good:#22c55e; }}
body {{ margin:0; font-family:system-ui,-apple-system,Segoe UI,sans-serif; background:var(--bg); color:#e5e7eb; }}
header {{ padding:22px 28px; border-bottom:1px solid var(--line); background:#020617; }}
h1 {{ margin:0 0 4px; font-size:24px; }} p {{ color:var(--muted); }} main {{ padding:22px; max-width:1200px; margin:auto; display:grid; gap:18px; }}
.grid {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:18px; }}
.card {{ background:var(--card); border:1px solid var(--line); border-radius:16px; padding:18px; box-shadow:0 10px 28px rgba(0,0,0,.25); }}
label {{ display:block; color:#cbd5e1; font-size:13px; margin:10px 0 4px; }}
input,select,button {{ width:100%; box-sizing:border-box; padding:10px 12px; border-radius:10px; border:1px solid var(--line); background:#020617; color:#e5e7eb; }}
button {{ background:var(--accent); border:0; color:white; font-weight:700; cursor:pointer; margin-top:12px; }}
.secondary {{ background:#334155; }} .pill {{ display:inline-block; padding:4px 9px; border-radius:999px; background:#1e293b; color:#cbd5e1; font-size:12px; }}
.msg {{ color:#fef08a; }} table {{ width:100%; border-collapse:collapse; font-size:13px; }} th,td {{ border-bottom:1px solid var(--line); padding:8px 6px; text-align:left; vertical-align:top; }} th {{ color:#cbd5e1; position:sticky; top:0; background:#111827; }} .table-wrap {{ max-height:520px; overflow:auto; }} code {{ color:#93c5fd; }}
</style></head><body>
<header><h1>Smart Classroom Controller</h1><p>Upload Excel timetables, expose JSON for ESP32, and optionally run AI cleanup.</p></header>
<main>{msg}<div class='grid'>
<section class='card'><h2>1. Upload timetable</h2><form method='post' action='/upload' enctype='multipart/form-data'><label>.xls or .xlsx</label><input type='file' name='file' accept='.xls,.xlsx' required><button>Parse timetable</button></form><p><span class='pill'>{len(entries)} entries parsed</span></p></section>
<section class='card'><h2>2. Room controller</h2><form method='post' action='/select-room'><label>Selected room for ESP32 default endpoint</label><select name='selected_room'>{options}</select><button>Set selected room</button></form><p>ESP32 calls <code>/api/current?sala=EC002</code> or <code>/api/current</code>.</p></section>
<section class='card'><h2>3. AI cleanup</h2><p>Status: <span class='pill'>{ai_status}</span></p><form method='post' action='/ai-cleanup'><button class='secondary'>Run AI normalization review</button></form><p>AI reviews parser output and normalizes course/professor names. Runtime remains deterministic.</p></section>
</div>
<section class='card'><h2>Manual entry</h2><form method='post' action='/manual' class='grid'>
<div><label>Day</label><select name='day_index'><option value='0'>LUNI</option><option value='1'>MARȚI</option><option value='2'>MIERCURI</option><option value='3'>JOI</option><option value='4'>VINERI</option></select></div>
<div><label>Start</label><input name='start' value='18:00'></div><div><label>End</label><input name='end' value='20:00'></div><div><label>Room</label><input name='room' value='EC 002'></div><div><label>Course</label><input name='course' value='N/A'></div><div><label>Professor</label><input name='professor' value='N/A'></div><div><label>Type</label><input name='activity_type' value='curs'></div><div><label>Group</label><input name='group' value='all'></div><div><label>&nbsp;</label><button>Add row</button></div>
</form></section>
<section class='card'><h2>Parsed schedule rows</h2><div class='table-wrap'><table><thead><tr><th>Day</th><th>Time</th><th>Room</th><th>Course</th><th>Professor</th><th>Type</th><th>Group</th><th>Source</th></tr></thead><tbody>{rows}</tbody></table></div></section>
</main></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def send_text(self, text: str, status=200, content_type="text/html; charset=utf-8"):
        data = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, payload, status=200):
        self.send_text(json.dumps(payload, ensure_ascii=False), status, "application/json; charset=utf-8")

    def redirect(self, location="/"):
        self.send_response(303)
        self.send_header("Location", location)
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)
        if parsed.path == "/":
            self.send_text(page_html())
        elif parsed.path == "/api/current":
            cfg = load_config()
            room = qs.get("sala", [cfg.get("selected_room", "EC 002")])[0]
            now = qs.get("now", [None])[0]
            self.send_json(current_payload(room, now))
        elif parsed.path == "/api/now":
            now = normalize_when(None)
            self.send_json({"server_now": now.strftime("%Y-%m-%d %H:%M"), "timezone": "Europe/Bucharest"})
        elif parsed.path == "/api/schedule":
            self.send_json(load_entries(SCHEDULE_JSON))
        elif parsed.path == "/api/selected":
            self.send_json(load_config())
        else:
            self.send_text("Not found", 404, "text/plain")

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        if parsed.path == "/upload":
            try:
                filename, data = self.extract_upload(body)
                path = UPLOADS / filename
                path.write_bytes(data)
                entries = parse_any_excel(path)
                save_entries(entries, SCHEDULE_JSON)
                self.send_text(page_html(f"Parsed {len(entries)} entries from {filename}."))
            except Exception as exc:
                self.send_text(page_html(f"Upload failed: {exc}"), 500)
        elif parsed.path == "/select-room":
            form = parse_qs(body.decode("utf-8", errors="ignore"))
            cfg = load_config()
            cfg["selected_room"] = form.get("selected_room", ["EC 002"])[0]
            save_config(cfg)
            self.redirect("/")
        elif parsed.path == "/manual":
            form = parse_qs(body.decode("utf-8", errors="ignore"))
            entries = load_entries(SCHEDULE_JSON)
            room = form.get("room", ["N/A"])[0].strip() or "N/A"
            day_index = int(form.get("day_index", ["0"])[0])
            days = ["LUNI", "MARȚI", "MIERCURI", "JOI", "VINERI", "SÂMBĂTĂ", "DUMINICĂ"]
            entries.append({
                "id": f"manual-{len(entries)+1}", "program": "Manual", "sheet": "Manual",
                "day": days[day_index], "day_index": day_index,
                "start": form.get("start", ["08:00"])[0], "end": form.get("end", ["09:00"])[0],
                "room": room, "room_key": normalize_room_key(room),
                "course": form.get("course", ["N/A"])[0].strip() or "N/A",
                "professor": form.get("professor", ["N/A"])[0].strip() or "N/A",
                "activity_type": form.get("activity_type", ["N/A"])[0].strip() or "N/A",
                "group": form.get("group", ["manual"])[0].strip() or "manual",
                "source_text": "manual dashboard entry", "source_cell": "manual",
            })
            SCHEDULE_JSON.write_text(json.dumps(entries, ensure_ascii=False, indent=2), encoding="utf-8")
            self.redirect("/")
        elif parsed.path == "/ai-cleanup":
            entries = load_entries(SCHEDULE_JSON)
            result = improve_entries_with_ai(entries)
            if result.get("entries"):
                SCHEDULE_JSON.write_text(json.dumps(result["entries"], ensure_ascii=False, indent=2), encoding="utf-8")
            self.send_text(page_html(result.get("message", "AI cleanup complete.")))
        else:
            self.send_text("Not found", 404, "text/plain")

    def extract_upload(self, body: bytes):
        ctype = self.headers.get("Content-Type", "")
        m = re.search(r"boundary=(.+)", ctype)
        if not m:
            raise ValueError("No multipart boundary found")
        boundary = ("--" + m.group(1)).encode()
        for part in body.split(boundary):
            if b'filename="' not in part:
                continue
            header, _, data = part.partition(b"\r\n\r\n")
            if not data:
                continue
            fname_match = re.search(br'filename="([^"]+)"', header)
            if not fname_match:
                continue
            filename = Path(fname_match.group(1).decode("utf-8", errors="ignore")).name
            data = data.rstrip(b"\r\n-")
            return filename, data
        raise ValueError("No uploaded file found")


def run(host="0.0.0.0", port=5000):
    print(f"Smart Classroom Controller running on http://{host}:{port}")
    ThreadingHTTPServer((host, port), Handler).serve_forever()


if __name__ == "__main__":
    run(port=int(os.environ.get("PORT", "5000")))
