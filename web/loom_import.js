/* Loom preset adapter for the Loom FM-1 web editor.
 *
 * Imports:
 *   - Loom portable presets (.loom.json / .json, schema 1)
 *   - Helm patches (.helm, JSON, CC-BY 4.0)
 *   - Surge XT patches (.fxp, XML scene A)
 *
 * and converts them to FM-1 SUBTRACT / WAVETABLE patches in the editor's
 * "felucca-library" JSON shape, so they can be added to the local library and
 * stored to a device user slot with the normal librarian tools.
 *
 * FM-1 parameter ids follow firmware/src/core.h (P_COUNT 114, P_E0 106 for the
 * 1.5 + chord/arp extras layout). Values are positional by id; the editor's
 * readLibraryFile() remaps by count for other firmware layouts.
 */

export const FM1 = {
  P_LEVEL: 0,
  P_ATK: 1, P_DEC: 2, P_SUS: 3, P_REL: 4,
  P_ED_FLT: 5,
  P_VOICE: 37, P_GLIDE: 38, P_PAN: 39,
  P_DETUNE: 44,
  P_E0: 106, P_COUNT: 114,
  ENG_SUBTRACT: 0,
  ENG_WAVETABLE: 14,
};

const clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, x));
const num = (x, def = 0) => (Number.isFinite(+x) ? +x : def);
const byte = (x) => clamp(Math.round(num(x) * 127), 0, 127);
const bip = (x) => clamp(Math.round(num(x) * 63), -64, 63);

/* --------------------------------------------------------------- formats --- */

export function looksLikeHelm(text) {
  return text.includes('"settings"') && text.includes('"patch_name"');
}

export function looksLikeSurge(bytes) {
  if (!bytes || bytes.length < 8) return false;
  const head = String.fromCharCode(...bytes.slice(0, 4));
  if (head === "CcnK") return true;
  return findBytes(bytes, new TextEncoder().encode("<?xml")) >= 0;
}

export function looksLikeLoom(text) {
  try {
    const o = JSON.parse(text);
    return !!o && typeof o === "object" && typeof o.engine === "string" &&
      (o.parameters || o.engine);
  } catch (e) {
    return false;
  }
}

/* ------------------------------------------------------------- entrypoint --- */

export async function importPresetFile(file, ctx = {}) {
  const lower = (file.name || "").toLowerCase();
  const buf = new Uint8Array(await file.arrayBuffer());
  let loom = null;

  if (lower.endsWith(".fxp") || looksLikeSurge(buf)) {
    loom = surgeToLoom(buf);
  } else {
    const text = new TextDecoder().decode(buf);
    if (looksLikeHelm(text)) loom = helmToLoom(text);
    else loom = JSON.parse(text);
  }
  if (!loom || !loom.engine) throw new Error("not a Loom, Helm or Surge preset");

  const patch = toFm1Patch(loom, file.name || "Imported", ctx);
  if (!patch) throw new Error("unsupported engine (only Subtractive and Wavetable map to FM-1)");

  return {
    format: "felucca-library",
    version: 1,
    kind: "library",
    firmware: "",
    pCount: patch.params.length,
    pE0: FM1.P_E0,
    paramLabels: [],
    engines: [],
    exported: new Date().toISOString(),
    patches: [patch],
  };
}

/* --------------------------------------------------------------- FM-1 map --- */

function toFm1Patch(loom, fallbackName, ctx) {
  const p = new Array(ctx.keys && ctx.keys.length ? ctx.keys.length : FM1.P_COUNT).fill(null);
  const params = loom.parameters || {};
  const v = (id, def = 0) => {
    const x = params[id] ?? params[String(id)];
    return x == null ? def : num(x, def);
  };
  const name = (loom.name || fallbackName || "LOOM").slice(0, 12).toUpperCase();
  const engineKey = String(loom.engine || "").toLowerCase();

  p[FM1.P_LEVEL] = 104;
  p[FM1.P_VOICE] = 0;
  p[FM1.P_PAN] = 0;
  p[FM1.P_ED_PIT] = 0;
  p[FM1.P_ED_SHP] = 0;

  if (engineKey === "subtractive") {
    p[FM1.P_ATK] = byte(v(100, 0.01));
    p[FM1.P_DEC] = byte(v(101, 0.3));
    p[FM1.P_SUS] = byte(v(102, 0.8));
    p[FM1.P_REL] = byte(v(103, 0.3));
    p[FM1.P_ED_FLT] = bip(v(118, 0));
    p[FM1.P_GLIDE] = byte(v(355, 0));
    p[FM1.P_DETUNE] = byte(v(106, 0));

    p[FM1.P_E0 + 0] = 0;                       // OSC
    p[FM1.P_E0 + 1] = byte(v(104, 0.66));      // WAVE
    p[FM1.P_E0 + 2] = byte(v(180, 0));         // FOLD
    p[FM1.P_E0 + 3] = clamp(32 + byte(v(108, 0.4)), 0, 127); // MIX
    p[FM1.P_E0 + 4] = byte(v(1, 0.8));         // CUT
    p[FM1.P_E0 + 5] = byte(v(113, 0.1));       // RES
    p[FM1.P_E0 + 6] = byte(v(170, 0));         // DRV
    p[FM1.P_E0 + 7] = 64;                      // KTR

    return libraryPatch(name, FM1.ENG_SUBTRACT, "SUBTRACT", p, loom);
  }

  if (engineKey === "wavetable") {
    p[FM1.P_ATK] = byte(v(454, 0.01));
    p[FM1.P_DEC] = byte(v(455, 0.3));
    p[FM1.P_SUS] = byte(v(456, 0.8));
    p[FM1.P_REL] = byte(v(457, 0.3));
    p[FM1.P_ED_FLT] = bip(v(464, 0));
    p[FM1.P_GLIDE] = byte(v(355, 0));
    p[FM1.P_DETUNE] = byte(v(451, 0));

    p[FM1.P_E0 + 0] = sourceIndex(loom.wavetableName); // SRC
    p[FM1.P_E0 + 1] = byte(v(450, 0));         // POS
    p[FM1.P_E0 + 2] = bip(v(465, 0));          // WARP
    p[FM1.P_E0 + 3] = byte(v(451, 0));         // DETN
    p[FM1.P_E0 + 4] = 127 - byte(v(475, 0));   // BITS (Loom 0 = clean)
    p[FM1.P_E0 + 5] = byte(v(476, 0));         // SRAT
    p[FM1.P_E0 + 6] = byte(v(467, 0));         // DRV
    p[FM1.P_E0 + 7] = byte(v(355, 0));         // GLID

    return libraryPatch(name, FM1.ENG_WAVETABLE, "WAVETABLE", p, loom);
  }

  return null;
}

function libraryPatch(name, engine, engineName, p, loom) {
  return {
    name,
    engine,
    engineName,
    params: p,
    pattern: null,
    tags: ["loom", "import"],
    created: new Date().toISOString(),
    modified: new Date().toISOString(),
    _source: loom.source || "Loom",
  };
}

function sourceIndex(table) {
  const n = String(table || "").toUpperCase();
  if (n.includes("PIANO")) return 1;
  if (n.includes("FLUTE")) return 2;
  if (n.includes("SAX")) return 3;
  if (n.includes("BREAK")) return 4;
  if (n.includes("USR1")) return 5;
  if (n.includes("USR2")) return 6;
  if (n.includes("USR3")) return 7;
  return 0; // BASIC
}

/* ------------------------------------------------------------------ Helm --- */

export function helmToLoom(text) {
  const root = JSON.parse(text);
  const s = root.settings || {};
  const name = root.patch_name || "Helm Import";
  const folder = root.folder_name || "Import";
  const author = root.author || "Helm";

  const f = (k, d = 0) => num(s[k], d);
  const osc1Level = f("osc_1_volume", 0.6);
  const osc2Level = f("osc_2_volume", 0);
  const subLevel = f("sub_volume", 0);
  const noiseLevel = f("noise_volume", 0);
  const anySource = osc1Level + osc2Level + subLevel + noiseLevel;

  const p = {};
  p[104] = helmWave(f("osc_1_waveform", 0));
  p[105] = helmWave(f("osc_2_waveform", 0));
  p[155] = helmWave(f("sub_waveform", 0));
  p[107] = anySource < 0.01 ? 0.8 : osc1Level;
  p[108] = osc2Level;
  p[109] = subLevel;
  p[110] = noiseLevel;
  p[160] = pitchParam(f("osc_1_transpose", 0) + f("osc_1_tune", 0) / 100, 4);
  p[161] = pitchParam(f("osc_2_transpose", 0) + f("osc_2_tune", 0) / 100, 4);
  p[162] = pitchParam(f("sub_octave", 0) * 12, 2);
  p[106] = clamp(Math.max(f("osc_1_unison_detune", 0), f("osc_2_unison_detune", 0)) / 200, 0, 0.5);

  const filterOn = f("filter_on", 1) > 0.5;
  p[1] = filterOn ? helmCutoff(f("cutoff", 96)) : 1;
  p[113] = clamp(f("resonance", 0) * 0.6, 0, 0.6);
  p[118] = clamp(f("fil_env_depth", 0) / 96, 0, 1);
  p[100] = attackNorm(f("amp_attack", 0));
  p[101] = decayNorm(f("amp_decay", 0.2));
  p[102] = clamp(f("amp_sustain", 1), 0, 1);
  p[103] = decayNorm(f("amp_release", 0.1));
  p[114] = attackNorm(f("fil_attack", 0));
  p[115] = decayNorm(f("fil_decay", 0.2));
  p[116] = clamp(f("fil_sustain", 1), 0, 1);
  p[117] = decayNorm(f("fil_release", 0.1));
  p[355] = f("portamento", -9) <= -8 ? 0 : clamp(f("portamento", -9) / 12, 0, 1);

  return {
    schema: 1,
    name,
    engine: "subtractive",
    category: "Helm",
    author: `Helm / ${folder}`,
    description: `Imported from Helm (${folder}).`,
    parameters: p,
    filterMode: 0,
    source: `Helm patch "${name}" by ${author} (CC-BY 4.0)`,
  };
}

function helmWave(w) {
  switch (Math.round(w)) {
    case 0: return 0;
    case 1: return 0.33;
    case 2: return 1;
    case 3: case 4: return 0.66;
    case 5: return 0.75;
    case 6: return 0.8;
    case 7: return 0.85;
    case 8: return 0.6;
    case 9: return 0.7;
    case 10: return 0.8;
    default: return 0.66;
  }
}

function helmCutoff(note) {
  const hz = 440 * Math.pow(2, (note - 69) / 12);
  return clamp(Math.sqrt(clamp((hz - 20) / 14000, 0, 1)), 0, 1);
}

function pitchParam(semitones, divisor) {
  return clamp(Math.pow(2, semitones / 12) / divisor, 0, 1);
}

function attackNorm(seconds) {
  return clamp(Math.cbrt(clamp(seconds, 0.0001, 4) / 2), 0.001, 1);
}

function decayNorm(seconds) {
  return clamp(Math.cbrt(clamp(seconds, 0.0001, 6) / 3), 0.001, 1);
}

/* ----------------------------------------------------------------- Surge --- */

export function surgeToLoom(bytes) {
  const start = findBytes(bytes, new TextEncoder().encode("<?xml"));
  if (start < 0) return null;
  const endTag = new TextEncoder().encode("</patch>");
  const end = findBytes(bytes, endTag, start);
  const slice = bytes.slice(start, end >= 0 ? end + endTag.length : bytes.length);
  const xml = new TextDecoder().decode(slice);
  if (!xml.includes("<patch") || !xml.includes("<parameters>")) return null;

  /* The Surge patch XML is a flat list of <name type=".." value=".."/> tags
   * (some have a <modrouting/> child). A tiny regex extractor keeps this
   * module usable in Node tests and in the browser without a DOM. */
  const metaTag = /<meta\b([^>]*)\/?>/.exec(xml);
  const meta = metaTag ? parseAttrs(metaTag[1]) : {};
  const name = meta.name || "Surge Import";
  const category = meta.category || "Import";
  const author = meta.author || "Surge";

  const params = {};
  const tagRe = /<([A-Za-z0-9_]+)(\s[^>]*?)\/?>/g;
  let m;
  while ((m = tagRe.exec(xml))) {
    if (!m[2].includes("value=")) continue;
    const a = parseAttrs(m[2]);
    if (a.value == null || a.type == null) continue;
    params[m[1]] = Number(a.value);
  }

  const f = (k, d = 0) => num(params[k], d);
  const i = (k, d = 0) => Math.round(num(params[k], d));
  const oscType = i("a_osc1_type", 0);
  const p0 = f("a_osc1_param0", 0.5);
  const p1 = f("a_osc1_param1", 0.5);
  const p2 = f("a_osc1_param2", 0.5);
  const p3 = f("a_osc1_param3", 0);

  const morph = (() => {
    if (oscType === 0) return clamp(p0, 0, 1);
    if (oscType === 1) return 0;
    if (oscType === 2 || oscType === 8) return clamp(p0, 0, 1);
    if (oscType === 3) return 0.9;
    if (oscType === 7) return windowShape(p0);
    if (oscType === 9) return 0.5;
    if (oscType === 10) return 0.7;
    if (oscType === 11) return 0.6;
    return 0.5;
  })();
  const warp = [2, 8, 10].includes(oscType) ? clamp(p1, 0, 1) : (oscType === 3 ? 0.25 : 0);
  const detune = [2, 7, 8].includes(oscType) ? clamp(p2 * 0.5, 0, 0.5) : 0.03;
  const drive = clamp(f("a_ws_drive", 0) * 0.6 + p3 * 0.2, 0, 1);
  const crush = i("a_ws_type", 0) !== 0 ? 0.2 : 0;
  const bits = oscType === 11 ? 0.35 : 0;

  const filterType = i("a_filter1_type", 0);
  const filterOn = filterType !== 0;
  const cutoff = filterOn ? surgeCutoff(f("a_filter1_cutoff", 0)) : 0.95;
  const resonance = filterOn ? clamp(f("a_filter1_resonance", 0) * 0.6, 0, 0.6) : 0.05;
  const filterEnv = filterOn ? clamp(f("a_filter1_envmod", 0) / 96, 0, 1) : 0;

  const out = {
    450: morph, 465: warp, 466: crush, 467: drive, 451: detune,
    475: bits, 476: 0,
    458: cutoff, 459: resonance, 464: filterEnv, 470: surgeFilterMode(filterType),
    454: attackNorm(surgeTime(f("a_env1_attack", -8))),
    455: decayNorm(surgeTime(f("a_env1_decay", 0))),
    456: clamp(f("a_env1_sustain", 1), 0, 1),
    457: decayNorm(surgeTime(f("a_env1_release", -5))),
    471: attackNorm(surgeTime(f("a_env2_attack", -8))),
    472: decayNorm(surgeTime(f("a_env2_decay", -2))),
    473: clamp(f("a_env2_sustain", 0), 0, 1),
    474: decayNorm(surgeTime(f("a_env2_release", -2))),
    355: 0,
  };

  return {
    schema: 1,
    name,
    engine: "wavetable",
    category: "Surge",
    author: `Surge / ${category}`,
    description: `Imported from Surge XT (scene A), oscillator type ${oscType}.`,
    parameters: out,
    filterMode: surgeFilterMode(filterType),
    wavetableName: "Basic",
    source: `Surge XT patch "${name}" by ${author} (GPLv3)`,
  };
}

function parseAttrs(text) {
  const out = {};
  const re = /([A-Za-z0-9_]+)="([^"]*)"/g;
  let m;
  while ((m = re.exec(text))) out[m[1]] = m[2]
    .replace(/&quot;/g, '"').replace(/&apos;/g, "'")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
  return out;
}

function surgeTime(v) {
  return Math.pow(2, clamp(v, -10, 6));
}
function surgeCutoff(v) {
  const hz = 440 * Math.pow(2, clamp(v, -60, 70) / 12);
  return clamp(Math.sqrt(clamp((hz - 20) / 14000, 0, 1)), 0, 1);
}
function surgeFilterMode(t) {
  if ([4, 5].includes(t)) return 1; // HP
  if ([6, 7].includes(t)) return 2; // BP
  if (t === 8) return 3;            // notch
  return 0;                          // LP / off
}
function windowShape(v) {
  switch (Math.round(v)) {
    case 0: return 0.33;
    case 1: return 0;
    case 5: return 0.66;
    case 6: return 0;
    case 7: case 8: return 1;
    default: return 0.5;
  }
}

/* ----------------------------------------------------------------- utils --- */

function findBytes(haystack, needle, from = 0) {
  if (!needle.length || haystack.length < needle.length) return -1;
  outer: for (let i = from; i <= haystack.length - needle.length; i++) {
    for (let j = 0; j < needle.length; j++) {
      if (haystack[i + j] !== needle[j]) continue outer;
    }
    return i;
  }
  return -1;
}
