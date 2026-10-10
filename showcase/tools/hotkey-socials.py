"""tests.json -> <Char>_test_socials.ini + <Char>_test_guide.md.
usage: python3 hotkey-socials.py tests.json <CharName> <outdir>
Pages 4-10, buttons 1-10 are tests, 11 PASS, 12 FAIL. {me} -> CharName."""
import json, sys, os

src, ME, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
cfg = json.load(open(src))
CH, BUILD = cfg["channel"], cfg["build"]
PAGES = list(range(4, 11))
slots = [(p, b) for p in PAGES for b in range(1, 11)]

def prefix(tid):
    for k in sorted(cfg["changes"], key=len, reverse=True):
        if tid.startswith(k) and tid[len(k):].isdigit():
            return k
    return None

buttons = [("LEAVE x5", "", "leave channel 1 five times", ["/leave 1"] * 5),
           ("JOIN", "", "join the test channel as /1",
            ["/join " + CH, "/1 T0 START test run, build " + BUILD.split()[-1], "/log [T0] START"])]
for tid, name, see, cmds in cfg["tests"]:
    cmds = [c.replace("{me}", ME) for c in cmds]
    buttons.append((name, tid, see, [f"/1 {tid}: {see}", f"/log [{tid}] target=%t id=%tid"] + cmds))
assert len(buttons) <= len(slots), f"{len(buttons)} buttons > {len(slots)} slots"

ini = ["[Socials]"]
def add(p, b, name, lines):
    assert len(name) <= 15 and 1 <= len(lines) <= 5, (name, lines)
    ini.extend([f"Page{p}Button{b}Name={name}", f"Page{p}Button{b}Color=0"])
    for i, l in enumerate(lines, 1):
        assert len(l) <= 63, (len(l), l)
        ini.append(f"Page{p}Button{b}Line{i}={l}")

used = {}
for (p, b), (name, tid, see, lines) in zip(slots, buttons):
    add(p, b, name, lines)
    used.setdefault(p, []).append((b, name, tid, see))
for p in used:
    add(p, 11, "PASS", ["/1 [RESULT] PASS"])
    add(p, 12, "FAIL", ["/1 [RESULT] FAIL - now type /1 and what you saw"])

first, last = min(used), max(used)
md = [f"# {ME} test hotkeys — {BUILD}", "",
      "Every test button (1) posts what you should see into channel 1 (the test channel), (2) writes "
      "`[test id] target=… id=…` into your log, (3) runs the test. After you look, press **PASS** (button 11) "
      "or **FAIL** (button 12) on the same page. On a FAIL, type `/1` and a few words about what you saw.", "",
      "## Install (once)", "",
      "1. Install the test build: Mimic → Settings → Zeal → Test build → Install. Put `EUR.tga` from "
      "wolfpack.quest/zeal-icons in `EverQuest\\uifiles\\zeal\\tagicons\\` (for TI2).",
      f"2. **Log {ME} out to character select or close EQ** (EQ rewrites the file when you camp).",
      f"3. Open `EverQuest\\{ME}_pq.proj.ini` in Notepad and find `[Socials]`. **Delete every `Page{first}…` "
      f"to `Page{last}…` line** (this replaces the earlier page 7–10 file too), then paste everything from "
      f"`{ME}_test_socials.ini` under `[Socials]` (skip its first line). No `[Socials]` but a "
      f"`Socials_{ME}_pq.proj.ini` file? Paste there instead.",
      f"4. Log in, `/log on`, open Actions → Socials, page {first}. Press **LEAVE x5**, then **JOIN**: the "
      "test channel is now `/1`.", "",
      "Afterwards, camp or relog to get your normal channels back. **Rejoin the Zeal tag channel before any "
      "`/tag chat` test with another person.**", "",
      f"Send me `eqlog_{ME}_pq.proj.txt` (or the part from `[T0]` on) and `{ME}-PoPFlags.txt` from the "
      "EverQuest folder. I'll tell you which tests have no "
      "PASS/FAIL, check the spawn ids and the `/ari` `/arl` status lines, and turn it into the testing "
      "evidence for each pull request.", "",
      "## What each test covers", "", "| Tests | Pull request |", "|---|---|"]
for k, (slug, title) in cfg["changes"].items():
    ids = [t[0] for t in cfg["tests"] if prefix(t[0]) == k]
    if ids:
        md.append(f"| {ids[0]}–{ids[-1]} | {title} |")
for p, rows in used.items():
    md += ["", f"## Page {p}", "", "| Button | Test | You should see |", "|---|---|---|"]
    for b, name, tid, see in rows:
        md.append(f"| {b} | {name} | {see} |")
    md += ["| 11 | PASS | logs a pass |", "| 12 | FAIL | logs a fail — then `/1` a note |"]

os.makedirs(outdir, exist_ok=True)
open(os.path.join(outdir, f"{ME}_test_socials.ini"), "w", newline="\r\n").write("\n".join(ini) + "\n")
open(os.path.join(outdir, f"{ME}_test_guide.md"), "w").write("\n".join(md) + "\n")
unmapped = [t[0] for t in cfg["tests"] if t[0] != "T9" and not prefix(t[0])]
print(len(buttons), "buttons, pages", first, "-", last, "; unmapped:", unmapped)
