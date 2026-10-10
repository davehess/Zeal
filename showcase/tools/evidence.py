"""EQ log from a hotkey test run -> per-change EVIDENCE.md + a missing-result report.
usage: python3 -I evidence.py tests.json eqlog.txt outdir --names Name1,Name2
Reads from the LAST "[T0] START" on. --names are redacted to <tester> (first) / <player>."""
import json, re, sys, os, argparse

ap = argparse.ArgumentParser()
ap.add_argument("tests"); ap.add_argument("log"); ap.add_argument("outdir")
ap.add_argument("--names", default="")
a = ap.parse_args()
cfg = json.load(open(a.tests))
names = [n for n in a.names.split(",") if n]

def redact(s):
    for i, n in enumerate(names):
        s = re.sub(rf"\b{re.escape(n)}\b", "<tester>" if i == 0 else "<player>", s)
    return s

LINE = re.compile(r"^\[(\w{3} \w{3} +\d+ [\d:]+ \d{4})\] (.*)$")
# Test channel ("You tell wpzt:1, '...'") or /say ("You say, '...'"); a run made with /say on a new character
# whose say is refused still works from the /log lines alone.
CHAN = re.compile(r"^(?:You tell [^,]+:\d+|You say), '(.*)'$")
LOGT = re.compile(r"^\[(\w+)\] target=(.*?) id=(\S*)\s*$")
START = re.compile(r"^(\w+): (.*)$")

lines = open(a.log, encoding="latin-1").read().splitlines()
start = max((i for i, l in enumerate(lines) if "[T0] START" in l), default=None)
if start is None:
    sys.exit("no [T0] START in the log — was the START (or JOIN) button pressed with /log on?")

ids = [t[0] for t in cfg["tests"]]
expect = {t[0]: t[2] for t in cfg["tests"]}
runs = {}            # tid -> list of runs (a test can be pressed twice)
cur = None
for raw in lines[start:]:
    m = LINE.match(raw)
    if not m:
        continue
    ts, msg = m.group(1), m.group(2)
    c = CHAN.match(msg)
    if c:
        body = c.group(1)
        s = START.match(body)
        if s and s.group(1) in expect:
            cur = {"tid": s.group(1), "at": ts, "target": "", "id": "", "result": None, "notes": [], "out": []}
            runs.setdefault(cur["tid"], []).append(cur)
            continue
        if cur is None:
            continue
        if body.startswith("[RESULT] "):
            cur["result"] = "PASS" if body.startswith("[RESULT] PASS") else "FAIL"
        else:
            cur["notes"].append(body)
        continue
    t = LOGT.match(msg)
    if t and t.group(1) in expect:
        if cur is None or cur["tid"] != t.group(1) or cur["target"] or cur["id"]:   # no say line before it
            cur = {"tid": t.group(1), "at": ts, "target": "", "id": "", "result": None, "notes": [], "out": []}
            runs.setdefault(cur["tid"], []).append(cur)
        cur["target"], cur["id"] = t.group(2), t.group(3)
        continue
    if cur is None:
        continue
    if msg.startswith("[RESULT] "):          # /log [RESULT] PASS|FAIL
        cur["result"] = "PASS" if msg.startswith("[RESULT] PASS") else "FAIL"
        continue
    if msg.startswith("NOTE"):               # /log NOTE what I saw
        cur["notes"].append(msg[4:].strip())
        continue
    if msg.startswith("You tell ") or msg.startswith("You say") or msg.startswith("[T0]"):
        continue
    cur["out"].append(msg)

def prefix(tid):
    for k in sorted(cfg["changes"], key=len, reverse=True):
        if tid.startswith(k) and tid[len(k):].isdigit():
            return k

report, review = [], set()
missing = [i for i in ids if i not in runs]
noresult = [i for i in ids if i in runs and runs[i][-1]["result"] is None]
fails = [i for i in ids if i in runs and runs[i][-1]["result"] == "FAIL"]
report.append(f"run from {lines[start][1:25]}: {len(runs)}/{len(ids)} tests pressed")
report.append("not pressed: " + (", ".join(missing) or "none"))
report.append("no PASS/FAIL: " + (", ".join(noresult) or "none"))
report.append("FAIL: " + (", ".join(fails) or "none"))

os.makedirs(a.outdir, exist_ok=True)
for k, (slug, title) in cfg["changes"].items():
    mine = [i for i in ids if prefix(i) == k]
    if not mine:
        continue
    md = [f"# Testing evidence — {title}", "",
          f"*In-game run on the fork's {cfg['build']} build, from the tester's own EverQuest log "
          f"(`/log` lines written by test hotkeys; character names replaced). Run started {lines[start][1:25]}.*", "",
          "| Test | Expected | Result | Target (spawn id) | Note |", "|---|---|---|---|---|"]
    detail = []
    for i in mine:
        if i not in runs:
            md.append(f"| {i} | {expect[i]} | not run | | |")
            continue
        r = runs[i][-1]   # a test pressed several times (MA2: one press per mob) lists every target
        tgt = "; ".join(redact(x["target"]) + (f" ({x['id']})" if x["id"] else "") for x in runs[i])
        for x in runs[i]:
            if x["target"] and " " not in x["target"] and x["target"] not in names:
                review.add(x["target"])
        note = redact(" / ".join(n for x in runs[i] for n in x["notes"])).replace("|", "\\|")
        md.append(f"| {i} | {expect[i]} | {r['result'] or 'no result'} | {tgt.replace('|', '/')} | {note} |")
        for x in runs[i]:
            if x["out"]:
                detail += [f"**{i}** ({x['at']})", "```", *[redact(o) for o in x["out"]], "```", ""]
    if detail:
        md += ["", "## What Zeal printed", ""] + detail
    d = os.path.join(a.outdir, slug)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "EVIDENCE.md"), "w").write("\n".join(md) + "\n")

if review:
    report.append("CHECK these one-word targets are NPCs, else add to --names: " + ", ".join(sorted(review)))
print("\n".join(report))
