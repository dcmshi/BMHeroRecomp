"""Measure where the arena floor lands on screen in a capture-game.ps1 PNG.

Turning "the framing looks wrong" into numbers. The Nitros floor is dark grey
tiles with green grid lines; the surrounding void is a bright, saturated green
swirl. Dark pixels are therefore floor, and their bounding box + centroid is
where the arena is on screen.

Pure stdlib (no PIL): PNG is zlib-compressed scanlines with a per-row filter.
Usage: python tools/shot_measure.py <png> [<png> ...]
"""
import sys, zlib, struct

def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, b'', None
    while pos < len(data):
        ln, typ = struct.unpack('>I4s', data[pos:pos+8])
        body = data[pos+8:pos+8+ln]
        if typ == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
            nch = 3 if color == 2 else 4
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
        pos += 12 + ln
    raw, stride, out, prev = zlib.decompress(idat), w * nch, [], bytearray(w * nch)
    p = 0
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p+1:p+1+stride]); p += 1 + stride
        for i in range(stride):                      # undo the row filter
            a = line[i-nch] if i >= nch else 0
            b = prev[i]
            c = prev[i-nch] if i >= nch else 0
            if   f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b-c), abs(a-c), abs(a+b-2*c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out.append(bytes(line)); prev = line
    return w, h, nch, out

def measure(path, top=62):
    """top: rows of custom title bar to skip (the game viewport starts below)."""
    w, h, nch, rows = read_png(path)
    xs, ys, n = 0, 0, 0
    minx, maxx, miny, maxy = w, -1, h, -1
    for y in range(top, h):
        row = rows[y]
        for x in range(w):
            r, g, b = row[x*nch], row[x*nch+1], row[x*nch+2]
            # Classifier from sampled pixels, not guesswork: the Nitros floor is
            # BLUE-grey stone (b > g, e.g. 41,50,59 / 55,67,79) and the surrounding
            # void is a GREEN-cyan swirl (g >= b, e.g. 25,134,101 / 4,64,45) at
            # every brightness, so one channel comparison separates them. The red
            # HUD bars are r-dominant and fall out as "not floor" automatically.
            if b > g:
                xs += x; ys += y; n += 1
                minx, maxx = min(minx, x), max(maxx, x)
                miny, maxy = min(miny, y), max(maxy, y)
    if n == 0:
        return f"{path}: NO FLOOR PIXELS"
    return (f"{path}: floor px={n:6d} ({100.0*n/(w*(h-top)):5.1f}% of viewport)  "
            f"centroid=({xs/n:6.1f},{ys/n:6.1f})  bbox x[{minx},{maxx}] y[{miny},{maxy}]")

for p in sys.argv[1:]:
    print(measure(p))


def find_player(path, top=62):
    """Locate the bomberman puppet: it is the only near-WHITE thing on screen.

    Tiles are dark blue-grey, the void is green-cyan, the HUD bars are red, and
    the tile decorations are saturated blue/green - all have at least one channel
    near zero. Requiring min(r,g,b) high isolates the white bomber body.

    The player's WORLD position is known exactly (the sim drives it), so its pixel
    position is a hard world->screen correspondence: it validates or refutes the
    projection model instead of leaving us to argue about screenshots.
    """
    w, h, nch, rows = read_png(path)
    xs = ys = n = 0
    for y in range(top, h):
        row = rows[y]
        for x in range(w):
            r, g, b = row[x*nch], row[x*nch+1], row[x*nch+2]
            if min(r, g, b) > 185:
                xs += x; ys += y; n += 1
    return (None if n == 0 else (xs/n, ys/n, n))

if __name__ == '__main__':
    pass
