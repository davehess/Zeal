// Renders every showcase poster (1200x675) and PR header banner (1280x320) from tools/poster.html and the
// JSON files in showcase/posters/, plus any <change>/diagram.json to <change>/diagram.png.
//   NODE_PATH=$(npm root -g) node showcase/tools/render-posters.js [name ...]
// With no names it renders everything. CHROMIUM overrides the browser path.
const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const root = path.resolve(__dirname, "..");            // .../showcase
const posters = path.join(root, "posters");
const exe = process.env.CHROMIUM || (fs.existsSync("/opt/pw-browsers/chromium") ? "/opt/pw-browsers/chromium" : undefined);
const only = process.argv.slice(2);
const url = (f) => "file://" + f;
const BUILD = "https://github.com/davehess/Zeal/releases/tag/test-all-build";

// Rewrites the table between the markers in showcase/README.md from every poster JSON (title, status, pr_url,
// branch, try). Everything outside the markers is hand-written and left alone.
function writeIndex() {
  const all = fs.readdirSync(posters).filter((f) => f.endsWith(".json"))
    .map((f) => ({ slug: f.slice(0, -5), ...JSON.parse(fs.readFileSync(path.join(posters, f), "utf8")) }))
    .sort((a, b) => (a.order || 99) - (b.order || 99));
  const rows = all.map((d) => {
    const pr = d.pr_url ? "[" + d.pr_url.replace(/^https:\/\/github\.com\//, "") + "](" + d.pr_url + ")" : "";
    const build = /^not in/.test(d.try || "") ? "not yet" : "[test-all](" + BUILD + ")";
    return "| [<img src=\"posters/" + d.slug + "-banner.png\" width=\"320\" alt=\"" + d.title + "\">](posters/" + d.slug + ".png) | " +
      "**[" + d.title + "](" + d.slug + "/README.md)**<br>" + d.pitch + " | " + (d.status || "draft") + "<br>`" + (d.branch || "") + "` | " +
      pr + " | " + build + " | [cases](" + d.slug + "/TEST-CASES.md) |";
  });
  const table = ["| Banner | Change | Status | PR | Build | Tests |", "|---|---|---|---|---|---|", ...rows].join("\n");
  const file = path.join(root, "README.md");
  const md = fs.readFileSync(file, "utf8");
  const a = "<!-- index:start -->", b = "<!-- index:end -->";
  const i = md.indexOf(a), j = md.indexOf(b);
  if (i < 0 || j < 0) throw new Error("README.md is missing the index markers");
  fs.writeFileSync(file, md.slice(0, i + a.length) + "\n" + table + "\n" + md.slice(j));
  console.log("index    README.md (" + all.length + " changes)");
}

(async () => {
  const browser = await chromium.launch({ executablePath: exe, args: ["--no-sandbox", "--allow-file-access-from-files"] });
  const page = await browser.newPage();
  const names = fs.readdirSync(posters).filter((f) => f.endsWith(".json")).map((f) => f.slice(0, -5))
    .filter((n) => !only.length || only.includes(n));

  // Diagrams first: a poster may use one as its image.
  const dpage = await browser.newPage({ viewport: { width: 1200, height: 400 } });
  await dpage.goto(url(path.join(root, "tools", "diagram.html")));
  for (const n of names) {
    const dj = path.join(root, n, "diagram.json");
    if (!fs.existsSync(dj)) continue;
    await dpage.evaluate((d) => window.renderDiagram(d), JSON.parse(fs.readFileSync(dj, "utf8")));
    await dpage.screenshot({ path: path.join(root, n, "diagram.png"), clip: { x: 0, y: 0, width: 1200, height: 400 } });
    console.log("diagram  " + n + "/diagram.png");
    await dpage.setViewportSize({ width: 800, height: 500 });
    await dpage.evaluate((d) => window.renderMini(d), JSON.parse(fs.readFileSync(dj, "utf8")));
    await dpage.screenshot({ path: path.join(root, n, "diagram-mini.png"), clip: { x: 0, y: 0, width: 800, height: 500 } });
    console.log("diagram  " + n + "/diagram-mini.png (posters and banners use this one)");
    await dpage.setViewportSize({ width: 1200, height: 400 });
  }

  await page.goto(url(path.join(root, "tools", "poster.html")));
  for (const n of names) {
    const data = JSON.parse(fs.readFileSync(path.join(posters, n + ".json"), "utf8"));
    if (data.image) data.image = url(path.join(root, data.image));   // image is relative to showcase/
    for (const [mode, w, h, suffix] of [["poster", 1200, 675, ""], ["banner", 1280, 320, "-banner"]]) {
      await page.setViewportSize({ width: w, height: h });
      await page.evaluate(([d, m]) => window.render(d, m), [data, mode]);
      await page.screenshot({ path: path.join(posters, n + suffix + ".png"), clip: { x: 0, y: 0, width: w, height: h } });
      console.log(mode.padEnd(8) + " posters/" + n + suffix + ".png");
    }
  }
  await browser.close();
  writeIndex();
})().catch((e) => { console.error(e); process.exit(1); });
