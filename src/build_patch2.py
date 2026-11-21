import json, os, sys, struct, glob

BASE = r"C:/Users/40467/Documents/deepseek-harness/default-workspace/work"
P4 = os.path.join(BASE, "p4")
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BASE, "p5", "build_corr")

corpus = [json.loads(l) for l in open(os.path.join(BASE, "p2", "corpus_v2.jsonl"), encoding="utf-8")]
nd = json.load(open(os.path.join(BASE, "p3", "namedict.json"), encoding="utf-8"))

def map_np(jp_np):
    if not jp_np: return ""
    if jp_np in nd: return nd[jp_np]
    parts = [p for p in jp_np.split("・") if p]
    if not parts: return ""
    return "・".join(nd.get(p, p) for p in parts)

trans = {}
for f in sorted(glob.glob(os.path.join(P4, "out", "*.jsonl"))):
    for l in open(f, encoding="utf-8"):
        l = l.strip()
        if not l:
            continue
        o = json.loads(l)
        if o.get("zh"):
            trans[o["id"]] = o["zh"]
print("translations:", len(trans))

# ---- corrections (proofread A-class) ----
corr = json.load(open(os.path.join(BASE, "p5", "corrections_all.json"), encoding="utf-8"))
_c = 0
for c in corr:
    if c["id"] in trans:
        trans[c["id"]] = c["new"]; _c += 1
print("corrections applied:", _c, "of", len(corr))


used = set()      # SJIS codes the original Japanese text actually contains
used_chars = set()  # characters the original text actually draws
for r in corpus:
    b = bytes.fromhex(r["key"]); i = 0
    try:
        used_chars.update(b.decode("cp932", "replace"))
    except Exception:
        pass
    while i < len(b):
        c = b[i]
        if (0x81 <= c <= 0x9F or 0xE0 <= c <= 0xEA) and i + 1 < len(b):
            used.add((c << 8) | b[i + 1]); i += 2
        else:
            used.add(c); i += 1
# Speaker names live in the palette/UI layer, never inside a corpus line.
used_chars.update("正樹やよいみちるまりかあきら啓介睦月ミツコ美優大須賀ヘルムート三島工藤大西坂上富田慎二高田"
                  "小野椎名優莉美沙風間宗像夕呼まりも祷子")

def _valid_sjis(code):
    lead = code >> 8; trail = code & 0xFF
    if not (0x81 <= lead <= 0x9F or 0xE0 <= lead <= 0xFC):
        return False
    return 0x40 <= trail <= 0x7E or 0x80 <= trail <= 0xFC

def _cp932_of(code):
    try:
        s = bytes([code >> 8, code & 0xFF]).decode("cp932")
        return s if len(s) == 1 else None
    except Exception:
        return None

def _safe_code(code):
    """A tunnel code must be invisible to the engine's *native* text path.

    cnpatch.dll keeps two maps:
      * find_uni()  : tunnel code -> the Chinese character to draw (forward).
      * build_rmap(): cp932(tunnel code) -> Chinese character (reverse).  The engine
        sometimes converts SJIS to Unicode before asking for a glyph, so the reverse
        map recovers the tunnel character from its Unicode form.

    The reverse map is keyed by the *Unicode* value of cp932(code), but
    GetGlyphOutlineA receives the raw two-byte SJIS code as an integer.  If
    cp932(code) happens to be U+82DC (苜), the reverse-map key is 0x82DC - exactly
    the SJIS code of 'ま'.  Every native 'ま' in the game is then drawn with the
    tunnel character's glyph (v1.18 bug: 【まりか】 rendered as 【６りか】).

    A code is therefore safe only when none of its three identities collide with
    text the engine draws natively:
      1. the code itself is unused by the original Japanese text;
      2. cp932(code) is a character the game never draws;
      3. if cp932(code) is a valid SJIS lead/trail pair, the character that pair
         encodes must also be one the game never draws.
    """
    if not _valid_sjis(code) or code == 0x8140:   # 0x8140 = native full-width space
        return False
    if code in used:                              # (1) shadows a native code
        return False
    s = _cp932_of(code)
    if s is None or s in used_chars:              # (2) its own glyph is drawn natively
        return False
    u = ord(s)
    if _valid_sjis(u):                            # (3) reverse-map key is a native code
        s2 = _cp932_of(u)
        if s2 is not None and s2 in used_chars:
            return False
    return True

_PREFERRED = [(0x989F, 0x9FFD), (0xE040, 0xEAA5)]
FREE = [c for lo, hi in _PREFERRED for c in range(lo, hi) if _safe_code(c)]
if len(FREE) < 3500:
    FREE += [c for c in range(0x8200, 0x9800) if _safe_code(c)]
if len(FREE) < 3500:
    FREE += [c for c in range(0xEAA5, 0xEFFD) if _safe_code(c)]
ci = [0]
tmap = {}
def encode(z):
    """Tunnel-encode every non-ASCII char.

    v2: CP932 contains a number of simplified-Chinese characters (个/怎/很/做/哦/愿/梦...)
    whose native codes overlap the unused-code pool.  An earlier version kept them native
    and then assigned the same codes to other characters, so the glyph hook rendered them
    as the tunnel character (e.g. 个->签, 愿->灿, 梦->哒).  Now everything >= 0x80 is
    tunneled, so no native code can collide; ASCII keeps its 1-byte form.
    """
    enc = bytearray()
    for ch in z:
        if ch == "‖": enc += b"\x03"; continue
        if ch == "⏎": enc += b"\x0a"; continue
        if ch == "　":                      # U+3000: keep the native SJIS space (0x8140).
            enc += b"\x81\x40"; continue     # A tunneled space has no outline and the engine
                                            # draws a white tofu box for it.
        if ch in ("【", "】"):
            # The engine only recognises a speaker name when the line starts with the
            # native SJIS brackets (it compares the raw bytes against 0x8175/0x8176),
            # so these two characters keep their native code even though every other
            # non-ASCII character is tunnel-encoded.  They still get a tunnel code
            # assigned so that all other codes keep their existing values.
            if ch not in tmap:
                tmap[ch] = FREE[ci[0]]; ci[0] += 1
            enc += b"\x81\x79" if ch == "【" else b"\x81\x7a"
            continue
        o = ord(ch)
        if o < 0x80:
            enc.append(o); continue
        if ch not in tmap:
            tmap[ch] = FREE[ci[0]]; ci[0] += 1
        enc += struct.pack(">H", tmap[ch])
    return enc

def encode_name(s):
    """Encode the speaker name.  Native SJIS is preferred so that the engine's name
    parser (which works on the raw SJIS bytes) still recognises the name; characters
    that CP932 cannot represent fall back to the tunnel code."""
    BAD_NATIVE = "凉广茉假儿"   # cp932 codes collide with CNUN tunnel codes
    out = bytearray()
    for ch in s:
        if ch not in BAD_NATIVE:
            try:
                e = ch.encode("cp932")
                if len(e) == 2:
                    out += e
                    continue
            except Exception:
                pass
        if ch not in tmap:
            tmap[ch] = FREE[ci[0]]; ci[0] += 1
        out += struct.pack(">H", tmap[ch])
    return bytes(out)

recs, overflow, missing, np_dropped = [], [], [], []
for r in corpus:
    rid = r["id"]
    zh = trans.get(rid)
    if zh is None:
        missing.append(rid); continue
    key = bytes.fromhex(r["key"])
    ks = key.decode("cp932", "replace")
    if ks.endswith("\x01"): ks = ks[:-1]
    jnp = ""
    if ks.startswith("【"):
        e = ks.find("】")
        if e > 0: jnp = ks[1:e]
    npz = map_np(jnp)
    np_full = b"\x81\x79" + encode_name(npz) + b"\x81\x7a" if npz else b""
    _zh = zh
    if _zh.startswith("【"):
        _e = _zh.find("】")
        if _e > 0: _zh = _zh[_e+1:]
    enc = bytearray(np_full) + encode(_zh)
    if len(enc) + 1 > len(key) and np_full:
        enc2 = encode(_zh)
        if len(enc2) + 1 <= len(key):
            np_dropped.append(rid); enc = enc2
    if len(enc) + 1 > len(key):
        overflow.append({"id": rid, "need": len(enc) + 1, "have": len(key), "zh": zh})
        continue
    recs.append((key, bytes(enc) + b"\x01"))

recs.sort(key=lambda kv: kv[0])

os.makedirs(OUTDIR, exist_ok=True)
blob = bytearray(); offsets = []
for k, v in recs:
    offsets.append(len(blob))
    blob += struct.pack("<H", len(k)) + k + struct.pack("<H", len(v)) + v
with open(os.path.join(OUTDIR, "cnpatch_txt.bin"), "wb") as f:
    f.write(b"CNTX"); f.write(struct.pack("<I", len(recs)))
    for o in offsets: f.write(struct.pack("<I", o))
    f.write(blob)
with open(os.path.join(OUTDIR, "cnpatch_uni.bin"), "wb") as f:
    f.write(b"CNUN"); f.write(struct.pack("<I", len(tmap)))
    for ch, code in sorted(tmap.items(), key=lambda kv: kv[1]):
        f.write(struct.pack("<HI", code, ord(ch)))
print("records:", len(recs), "overflow:", len(overflow), "missing:", len(missing), "np_dropped:", len(np_dropped), "tunnel:", len(tmap))
json.dump({"records": len(recs), "overflow": overflow, "missing": missing, "np_dropped": np_dropped,
           "txt_bytes": os.path.getsize(os.path.join(OUTDIR, "cnpatch_txt.bin")),
           "uni_bytes": os.path.getsize(os.path.join(OUTDIR, "cnpatch_uni.bin"))},
          open(os.path.join(BASE, "p5", "build_report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
if overflow:
    print("overflow ids:", [o["id"] for o in overflow][:20])
if np_dropped:
    print("np_dropped ids:", np_dropped[:20])
