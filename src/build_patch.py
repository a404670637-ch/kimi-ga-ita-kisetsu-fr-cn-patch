import json, os, sys, struct, glob

BASE = r"C:/Users/40467/Documents/deepseek-harness/default-workspace/work"
P4 = os.path.join(BASE, "p4")
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BASE, "p5", "dist", "cnpatch")

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

used = set()
for r in corpus:
    b = bytes.fromhex(r["key"]); i = 0
    while i < len(b):
        c = b[i]
        if (0x81 <= c <= 0x9F or 0xE0 <= c <= 0xEA) and i + 1 < len(b):
            used.add((c << 8) | b[i + 1]); i += 2
        else:
            used.add(c); i += 1
# Valid SJIS double-byte leads are 0x81-0x9F and 0xE0-0xFC; level-2 text codes live in
# 0x989F-0x9FFC and 0xE040-0xEAA4.  The 0xA000-0xDFFF gap is single-byte halfwidth kana,
# so codes there are NOT valid double-byte characters (v2 bug: text rendered as kana).
_FREE_RANGES = [(0x989F, 0x9FFD), (0xE040, 0xEAA5)]
def _valid_sjis(code):
    lead = code >> 8; trail = code & 0xFF
    if not (0x81 <= lead <= 0x9F or 0xE0 <= lead <= 0xFC):
        return False
    return 0x40 <= trail <= 0x7E or 0x80 <= trail <= 0xFC
used.add(0x8140)  # native full-width space must never become a tunnel code
FREE = [c for lo, hi in _FREE_RANGES for c in range(lo, hi) if _valid_sjis(c) and c not in used]
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
            enc += b"\x81\x75" if ch == "【" else b"\x81\x76"
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
    out = bytearray()
    for ch in s:
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
    enc = bytearray(np_full) + encode(zh)
    if len(enc) + 1 > len(key) and np_full:
        enc2 = encode(zh)
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
