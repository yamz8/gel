#!/bin/sh
# Runs every command-line command against the built binary: cli.sh <binary>
set -eu

GEL="$1"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
export HOME="$DIR/home"
mkdir -p "$HOME/.claude"
FAILED=0
PASSED=0

pass() { echo "PASS   : $1"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL!  : $1"; FAILED=$((FAILED + 1)); }
check() { # name, command...
    name="$1"; shift
    if "$@" >"$DIR/out" 2>"$DIR/err"; then pass "$name"; else fail "$name"; cat "$DIR/err"; fi
}
refuses() { # name, command...: must exit 1 with a message on stderr
    name="$1"; shift
    if "$@" >"$DIR/out" 2>"$DIR/err"; then fail "$name (succeeded)"; elif [ -s "$DIR/err" ]; then pass "$name"; else fail "$name (no message)"; fi
}
has() { # name, pattern: the last output contains it
    if grep -q -- "$2" "$DIR/out"; then pass "$1"; else fail "$1"; cat "$DIR/out"; fi
}

"$GEL" demo "$DIR/never" >/dev/null 2>&1 && fail "unknown command refused" || pass "unknown command refused"
check "help" "$GEL" help
has "help lists the commands" "quant <session|image>"

# A synthetic gel: three lanes of bright Gaussian bands on a dark background.
python3 - "$DIR/gel.png" <<'PY' || { fail "make a test gel"; exit 1; }
import struct, sys, zlib, math, random
w, h = 240, 200
random.seed(3)
rows = []
for y in range(h):
    row = bytearray([0])
    for x in range(w):
        v = 1500 + random.gauss(0, 30)
        for c in (50, 120, 190):
            if abs(x - c) <= 15:
                for b, a in ((60, 9000 * (c / 50)), (130, 4000)):
                    v += a * math.exp(-0.5 * ((y - b) / 4) ** 2)
        row += struct.pack(">H", max(0, min(65535, int(v))))
    rows.append(bytes(row))
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 0, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
BEFORE="$(sha256sum "$DIR/gel.png")"

check "lanes" "$GEL" lanes "$DIR/gel.png" --json
has "lanes finds three" '"lane": 3'
has "lanes reads 16 bits" '"bitDepth": 16'
check "lanes --save" "$GEL" lanes "$DIR/gel.png" --save
[ -f "$DIR/gel.gel" ] && pass "session saved next to the image" || fail "session saved next to the image"
check "check" "$GEL" check "$DIR/gel.gel" --json
has "check is clean" '"ok": true'
check "quant" "$GEL" quant "$DIR/gel.gel" --json
has "quant measures volumes" '"volume"'
has "quant finds six bands" '"band": 2'
check "quant an image without a session" "$GEL" quant "$DIR/gel.png"

# Normalize to lane 1, band 1 and mark nothing as ladder.
python3 - "$DIR/gel.gel" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d["quantification"].update(normalization="band", referenceLane=0, referenceBand=0)
d["lanes"][0]["label"] = "WT"
d["annotations"] = [{"kind": "text", "x": 100, "y": 20, "text": "β-actin"}]
json.dump(d, open(sys.argv[1], "w"))
PY
check "quant normalized" "$GEL" quant "$DIR/gel.gel" --json
has "reference band is 1" '"normalized": 1'
check "export png" "$GEL" export "$DIR/gel.gel" --png "$DIR/fig.png" --dpi 600
check "export svg, pdf, csv at once" "$GEL" export "$DIR/gel.gel" --svg "$DIR/fig.svg" --pdf "$DIR/fig.pdf" --csv "$DIR/r.csv"
head -c 4 "$DIR/fig.pdf" | grep -q "%PDF" && pass "pdf is a pdf" || fail "pdf is a pdf"
grep -q "WT" "$DIR/r.csv" && pass "csv has the labels" || fail "csv has the labels"
refuses "export needs a format" "$GEL" export "$DIR/gel.gel"
refuses "export refuses a bad dpi" "$GEL" export "$DIR/gel.gel" --png "$DIR/x.png" --dpi 5
refuses "export never overwrites the image" "$GEL" export "$DIR/gel.gel" --png "$DIR/gel.png"
[ "$(sha256sum "$DIR/gel.png")" = "$BEFORE" ] && pass "the image is unchanged" || fail "the image is unchanged"

python3 - "$DIR/gel.gel" "$DIR/bad.gel" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d["lanes"][0]["center"] = -50
d["ladder"]["lane"] = 9
json.dump(d, open(sys.argv[2], "w"))
PY
refuses "check finds errors" "$GEL" check "$DIR/bad.gel"
grep -q "outside the image" "$DIR/err" && pass "check names the lane" || fail "check names the lane"
refuses "missing file" "$GEL" quant "$DIR/nope.gel"
echo '{"format": "other"}' > "$DIR/other.gel"
refuses "not a session" "$GEL" check "$DIR/other.gel"

check "skill prints" "$GEL" skill
has "skill has its name" "name: gel"
check "skill install" "$GEL" skill install
[ -f "$HOME/.agents/skills/gel/SKILL.md" ] && [ -L "$HOME/.claude/skills/gel" ] && pass "skill installed and linked" || fail "skill installed and linked"

echo "Totals: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
