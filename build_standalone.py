import base64, re, os

mods = [
    ("hardwired-intro.mod", "HARDWIRED"),
    ("in-the-kitchen.mod",  "IN THE KITCHEN"),
    ("introfronty.mod",     "INTRO FRONTY"),
    ("monday.mod",          "MONDAY"),
    ("space-debris.mod",    "SPACE DEBRIS"),
]

html = open("index.html", "r", encoding="utf-8").read()

# Build embedded MODS array
lines = ["const MODS = ["]
for fname, label in mods:
    data = open("mods/" + fname, "rb").read()
    b64 = base64.b64encode(data).decode("ascii")
    lines.append("  { data: '" + b64 + "', label: '" + label + "' },")
lines.append("];")
new_mods = "\n".join(lines)

# Replace MODS array declaration
html = re.sub(r"const MODS = \[.*?\];", new_mods, html, flags=re.DOTALL)

# Replace the entire loadMod function body with an embedded-data version
old_load = r"async function loadMod\(idx\) \{.*?^\}"
new_load = """async function loadMod(idx) {
  if (!player) return;
  modNameEl.textContent = 'LOADING...';
  try {
    const b64 = MODS[idx].data;
    const binary = atob(b64);
    const raw = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) raw[i] = binary.charCodeAt(i);
    const mod = parseMod(raw);
    player.load(mod).play();
    modNameEl.textContent = `\u25b6  ${MODS[idx].label}  [${idx + 1}/${MODS.length}]`;
    modIdx = idx;
  } catch (e) {
    console.error('MOD load failed:', e);
    modNameEl.textContent = 'LOAD ERROR';
  }
}"""

html = re.sub(old_load, new_load, html, flags=re.DOTALL | re.MULTILINE)

out = "zealand-cracker-demo-standalone.html"
open(out, "w", encoding="utf-8").write(html)
print(f"Done: {os.path.getsize(out) // 1024} KB -> {out}")
