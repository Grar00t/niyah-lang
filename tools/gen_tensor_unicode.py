#!/usr/bin/env python3
"""Generate dependency-free Unicode NFKC tables from the build Python UCD."""
import hashlib
import json
import pathlib
import sys
import unicodedata as ud

root = pathlib.Path(sys.argv[1]).resolve()
out = root / "build/tensor_core"
out.mkdir(parents=True, exist_ok=True)
decomp, data, ccc, compose, marks = [], [], [], [], []
arabic_letters, arabic_marks = [], []
def append_range(ranges, cp):
    if ranges and ranges[-1][1]+1==cp:
        ranges[-1][1]=cp
    else:
        ranges.append([cp,cp])
for cp in range(0x110000):
    ch = chr(cp)
    if ud.name(ch, "").startswith("ARABIC"):
        if ud.category(ch).startswith("M"):
            append_range(arabic_marks,cp)
        elif ud.category(ch).startswith("L"):
            normalized=ud.normalize("NFKC",ch)
            if normalized and all(ud.category(c)[0] in "LM" for c in normalized):
                append_range(arabic_letters,cp)
    cc = ud.combining(ch)
    if cc:
        ccc.append((cp, cc))
    if ud.category(ch).startswith("M"):
        if marks and marks[-1][1] + 1 == cp:
            marks[-1][1] = cp
        else:
            marks.append([cp, cp])
    # Hangul has an algorithmic decomposition/composition in the native code.
    if not 0xAC00 <= cp <= 0xD7A3:
        seq = tuple(map(ord, ud.normalize("NFKD", ch)))
        if seq != (cp,):
            decomp.append((cp, len(data), len(seq)))
            data.extend(seq)
    raw = ud.decomposition(ch).split()
    if len(raw) == 2 and not raw[0].startswith("<"):
        a, b = (int(x, 16) for x in raw)
        if ud.normalize("NFC", chr(a) + chr(b)) == ch:
            compose.append(((a << 21) | b, cp))
compose.sort()
lines = ["/* Generated from Python Unicode " + ud.unidata_version + ". */",
         "#ifndef NIYAH_UNICODE_NFKC_H", "#define NIYAH_UNICODE_NFKC_H",
         '#define NIYAH_UNICODE_VERSION "' + ud.unidata_version + '"',
         "typedef struct { u32 cp, off, len; } NormDecomp;",
         "typedef struct { u32 cp, cc; } NormClass;",
         "typedef struct { u64 key; u32 cp; } NormCompose;",
         "typedef struct { u32 lo, hi; } NormRange;"]
def rows(name, typ, items):
    lines.append("static const " + typ + " " + name + "[] = {")
    lines.extend("{" + ",".join(str(x) + ("ULL" if typ == "NormCompose" and i == 0 else "u") for i, x in enumerate(row)) + "}," for row in items)
    lines.append("};")
rows("norm_decomp", "NormDecomp", decomp)
lines.append("static const u32 norm_data[] = {" + ",".join(str(x) + "u" for x in data) + "};")
rows("norm_ccc", "NormClass", ccc)
rows("norm_compose", "NormCompose", compose)
rows("norm_marks", "NormRange", marks)
rows("norm_arabic_letters", "NormRange", arabic_letters)
rows("norm_arabic_marks", "NormRange", arabic_marks)
lines.append("#endif")
blob = ("\n".join(lines) + "\n").encode()
(out / "niyah_unicode_nfkc.h").write_bytes(blob)
receipt = {"unicode_version": ud.unidata_version, "decompositions": len(decomp),
           "composition_pairs": len(compose), "mark_ranges": len(marks),
           "sha256": hashlib.sha256(blob).hexdigest()}
(out / "unicode_manifest.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt))
