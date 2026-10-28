#!/usr/bin/env python3
"""Bake Emberward's terrain and object textures from the generated source art.

Inputs (assets/source/):
  terrain-textures.png  2x2 sheet: grass, forest canopy, water, dirt road
  objects.png           3x3 sheet on magenta: two mountains, village, ruin,
                        fountain, elder, apothecary, herbalist, fisher
  buildings.png         3x2 sheet on magenta: cottage, timber house,
                        apothecary, inn, gatehouse, (unused) wall tower
  towers.png            2x2 sheet on magenta: NW, NE, SW, SE corner towers
  wall-textures.png     wall face masonry (left) and rampart walkway (right)
  battle-*.png          battle backdrops: grass, forest, road, shore, vault

Outputs (assets/):
  grass.png    seamless base ground, PERIOD x PERIOD tiles
  terrain.png  quadrant autotiles for water, road and forest
  objects.png  3x3 grid of keyed, shadowed 80x80 object cells
  walls.png    town wall autotiles (see build_walls)
  buildings.png 3x3 grid of 384x384 shadowed town building sprites
  battle.png   battle backdrops stacked vertically, one 1152x576 (3x) per scene

Terrain atlas layout: every map tile is drawn as four 32px quadrants
(TL, TR, BL, BR). Each quadrant has five states chosen from its horizontal,
vertical and diagonal neighbours (see STATE_* below). Because the textures
and edge noise are sampled in world space, a separate row is baked for each
position inside the texture period so neighbouring tiles always line up:

  x = (quadrant * 5 + state) * 32
  y = (terrain * PERIOD * PERIOD + (my % PERIOD) * PERIOD + (mx % PERIOD)) * 32

Requires Pillow and numpy (development only; the game itself only needs SDL3).
"""

from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "source"
OUT = ROOT / "assets"

TILE = 64
HALF = TILE // 2
PERIOD = 4
TEX = TILE * PERIOD

STATE_NONE, STATE_H, STATE_V, STATE_INNER, STATE_ALL = range(5)
TERRAINS = ("water", "road", "forest")


def load_textures():
    sheet = Image.open(SOURCE / "terrain-textures.png").convert("RGB")
    half = sheet.width // 2
    inset = 8
    boxes = {
        "grass": (0, 0),
        "forest": (half, 0),
        "water": (0, half),
        "road": (half, half),
    }
    textures = {}
    for name, (x, y) in boxes.items():
        crop = sheet.crop((x + inset, y + inset, x + half - inset, y + half - inset))
        crop = crop.resize((TEX, TEX), Image.LANCZOS)
        textures[name] = make_seamless(np.asarray(crop, dtype=np.float32))
    return textures


def make_seamless(tex):
    """Blend the texture into a half-period-rolled copy of itself near the edges."""
    n = tex.shape[0]
    rolled = np.roll(tex, (n // 2, n // 2), axis=(0, 1))
    idx = np.arange(n) + 0.5
    edge = np.minimum(idx, n - idx) / (n / 2)
    dist = np.minimum.outer(edge, edge)
    w = np.clip((dist - 0.12) / 0.35, 0.0, 1.0)
    w = (w * w * (3 - 2 * w))[..., None]
    return tex * w + rolled * (1 - w)


def smooth(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def periodic_noise(X, Y, seed, amp, bumps):
    """Organic edge noise, periodic over PERIOD tiles in world space."""
    rng = np.random.default_rng(seed)
    n = np.zeros_like(X)
    weights = (1.0, 0.7, 0.5, 0.35)
    for i, weight in enumerate(weights):
        fx, fy = rng.integers(1 + 2 * i, 3 + 3 * i, size=2)
        px, py = rng.uniform(0, 2 * np.pi, size=2)
        n += weight * (np.sin(2 * np.pi * fx * X / PERIOD + px) +
                       np.sin(2 * np.pi * fy * Y / PERIOD + py)) / 2
    # bounded so edge bands never reach the tile border
    n *= amp / sum(weights)
    if bumps:
        # rounded tree-crown scallops, `bumps` per tile
        n += bumps[1] * (np.abs(np.sin(np.pi * bumps[0] * X)) + np.abs(np.sin(np.pi * bumps[0] * Y)) - 1.6)
    return n


def quadrant_sd(state, u, v, inset, radius, inner_radius):
    """Signed distance (tile units, + inside) from the quadrant's outer corner frame."""
    if state == STATE_ALL:
        return np.full_like(u, 10.0)
    if state == STATE_H:
        return v - inset
    if state == STATE_V:
        return u - inset
    if state == STATE_INNER:
        # round the neighbouring terrain's convex corner; radius <= inset keeps
        # the quadrant borders identical to the adjacent STATE_H/STATE_V edges
        c = inset - inner_radius
        sd = np.maximum(u - inset, v - inset)
        corner = (u > c) & (v > c) & (u < inset) & (v < inset)
        return np.where(corner, np.hypot(u - c, v - c) - inner_radius, sd)
    c = inset + radius
    sd = np.minimum(u - inset, v - inset)
    corner = (u < c) & (v < c)
    round_sd = radius - np.hypot(u - c, v - c)
    return np.where(corner, round_sd, sd)


def shade(rgb, factor):
    return rgb * factor


def bake_terrain(name, texture, params, px, py, quadrant, state):
    ys, xs = np.mgrid[0:HALF, 0:HALF].astype(np.float32) + 0.5
    qx = (quadrant % 2) * HALF
    qy = (quadrant // 2) * HALF
    lx = (qx + xs) / TILE
    ly = (qy + ys) / TILE
    u = lx if quadrant % 2 == 0 else 1 - lx
    v = ly if quadrant // 2 == 0 else 1 - ly
    X = px + lx
    Y = py + ly

    sd = quadrant_sd(state, u, v, params["inset"], params["radius"], params["inner_radius"])
    if state != STATE_ALL:
        sd = sd + periodic_noise(X, Y, params["seed"], params["noise"], params.get("bumps"))

    tx = (px * TILE + qx + xs.astype(int)) % TEX
    ty = (py * TILE + qy + ys.astype(int)) % TEX
    rgb = texture[ty, tx].copy()
    aa = 1.0 / TILE
    alpha = np.clip(sd / aa + 0.5, 0.0, 1.0)

    if name == "road":
        rim = 1 - smooth(0.0, 0.045, sd)
        rgb = rgb * (1 - 0.28 * rim[..., None])
    elif name == "forest":
        rim = 1 - smooth(0.0, 0.05, sd)
        rgb = rgb * (1 - 0.35 * rim[..., None])
        shadow = smooth(-0.07, 0.0, sd) * (sd < 0) * 0.45
        dark = np.array([10, 30, 16], dtype=np.float32)
        out_a = alpha + shadow * (1 - alpha)
        rgb = np.where((sd < 0)[..., None], dark, rgb)
        alpha = out_a
    elif name == "water":
        foam = 1 - smooth(0.0, 0.04, sd)
        foam_rgb = np.array([210, 238, 245], dtype=np.float32)
        rgb = rgb * (1 - 0.75 * foam[..., None]) + foam_rgb * 0.75 * foam[..., None]
        sand_rgb = np.array([222, 196, 132], dtype=np.float32)
        wet = np.array([180, 156, 104], dtype=np.float32)
        sand_mix = smooth(-0.03, 0.0, sd)[..., None]
        sand = sand_rgb * (1 - sand_mix) + wet * sand_mix
        sand_a = smooth(-0.085, -0.065, sd) * (sd < 0)
        rgb = np.where((sd < 0)[..., None], sand, rgb)
        alpha = np.maximum(alpha, sand_a)

    out = np.zeros((HALF, HALF, 4), dtype=np.float32)
    out[..., :3] = rgb
    out[..., 3] = alpha * 255
    return out


TERRAIN_PARAMS = {
    "water": {"inset": 0.3, "radius": 0.2, "inner_radius": 0.28, "noise": 0.15, "seed": 3},
    "road": {"inset": 0.22, "radius": 0.24, "inner_radius": 0.2, "noise": 0.035, "seed": 11},
    "forest": {"inset": 0.2, "radius": 0.3, "inner_radius": 0.18, "noise": 0.07, "seed": 7,
               "bumps": (2, 0.045)},
}


def build_terrain(textures):
    rows = len(TERRAINS) * PERIOD * PERIOD
    atlas = np.zeros((rows * HALF, 20 * HALF, 4), dtype=np.float32)
    for t, name in enumerate(TERRAINS):
        for py in range(PERIOD):
            for px in range(PERIOD):
                row = t * PERIOD * PERIOD + py * PERIOD + px
                for q in range(4):
                    for s in range(5):
                        cell = bake_terrain(name, textures[name], TERRAIN_PARAMS[name], px, py, q, s)
                        x = (q * 5 + s) * HALF
                        atlas[row * HALF:(row + 1) * HALF, x:x + HALF] = cell
    return Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8))


def is_magenta(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return (r > 150) & (b > 150) & (g < 110) & (np.abs(r - b) < 90)


# Objects: 64x64 sprites inside 80x80 cells (left 4px, base 8px above the cell
# bottom) so the drop shadow fits. OBJECT_DRAW is each sprite's on-screen size
# in logical px (must match render.c) and keeps shadow offsets identical to the
# buildings regardless of scale. Villagers (5-8) get a footprint shadow.
OBJECT_CELL = 80
OBJECT_SPRITE = 64
OBJECT_DRAW = (20, 20, 22, 20, 32, 16, 16, 16, 16)


def build_objects():
    sheet, keyed_alpha = key_sheet(SOURCE / "objects.png")
    n = sheet.shape[0] // 3
    cell_out = OBJECT_SPRITE
    atlas = np.zeros((OBJECT_CELL * 3, OBJECT_CELL * 3, 4), dtype=np.float32)
    for i in range(9):
        cx, cy = (i % 3) * n, (i // 3) * n
        cell = sheet[cy + 4:cy + n - 4, cx + 4:cx + n - 4].copy()
        alpha = keyed_alpha[cy + 4:cy + n - 4, cx + 4:cx + n - 4]
        # pull any remaining pink spill toward neutral on the silhouette edge
        spill = np.clip((np.minimum(cell[..., 0], cell[..., 2]) - cell[..., 1] - 60) / 120, 0, 1)
        cell[..., 0] -= spill * (cell[..., 0] - cell[..., 1]) * 0.8
        cell[..., 2] -= spill * (cell[..., 2] - cell[..., 1]) * 0.8
        ys, xs = np.nonzero(alpha)
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        w, h = x1 - x0, y1 - y0
        side = max(w, h)
        square = np.zeros((side, side, 4), dtype=np.float32)
        ox = (side - w) // 2
        oy = side - h
        square[oy:oy + h, ox:ox + w, :3] = cell[y0:y1, x0:x1] * alpha[y0:y1, x0:x1, None]
        square[oy:oy + h, ox:ox + w, 3] = alpha[y0:y1, x0:x1] * 255
        # resize premultiplied, then unpremultiply
        img = Image.fromarray(np.clip(square, 0, 255).astype(np.uint8))
        img = img.resize((cell_out, cell_out), Image.LANCZOS)
        arr = np.asarray(img, dtype=np.float32)
        a = arr[..., 3:4] / 255
        arr[..., :3] = np.where(a > 0.01, arr[..., :3] / np.maximum(a, 0.01), 0)
        arr[..., 3] = np.where(arr[..., 3] < 40, 0, np.minimum(255, arr[..., 3] * 1.15))
        arr[..., 3] = np.where(arr[..., 3] < 128, 0, 255)
        cell_rgba = np.zeros((OBJECT_CELL, OBJECT_CELL, 4), dtype=np.float32)
        tile_px = OBJECT_SPRITE * 16 / OBJECT_DRAW[i]
        cell_rgba = with_shadow(cell_rgba, arr, 4, OBJECT_CELL - 8 - OBJECT_SPRITE, tile_px,
                                footprint=i >= 5)
        y, x = (i // 3) * OBJECT_CELL, (i % 3) * OBJECT_CELL
        atlas[y:y + OBJECT_CELL, x:x + OBJECT_CELL] = cell_rgba
    return Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8))


def dark_runs(profile, threshold):
    runs, start = [], None
    for i, dark in enumerate(profile < threshold):
        if dark and start is None:
            start = i
        elif not dark and start is not None:
            runs.append((start + i) / 2)
            start = None
    return runs


def masonry(panel, courses, bricks, width, height, first_course=1):
    """Rebuild a brick texture so it repeats exactly, cutting only at mortar joints."""
    lum = panel.mean(axis=2)
    rows = lum.mean(axis=1)
    lines = [int(round(c)) for c in dark_runs(rows, 0.8 * np.median(rows))]
    lines = [c for c in lines if 2 < c < panel.shape[0] - 2]
    edges = np.round(np.linspace(0, height, courses + 1)).astype(int)
    out = np.zeros((height, width, 3), dtype=np.float32)
    for c in range(courses):
        pieces, remaining, source = [], bricks, first_course + c
        while remaining:
            top, bottom = lines[source % (len(lines) - 1)], lines[source % (len(lines) - 1) + 1]
            source += courses
            if bottom <= top:
                continue
            inner = lum[top + (bottom - top) // 4:bottom - (bottom - top) // 4].mean(axis=0)
            joints = [int(round(j)) for j in dark_runs(inner, 0.8 * np.median(inner))]
            take = min(remaining, len(joints) - 1)
            if take < 1:
                continue
            piece = Image.fromarray(np.clip(panel[top:bottom, joints[0]:joints[take]], 0, 255)
                                    .astype(np.uint8))
            pieces.append(piece.resize((width * take // bricks, int(edges[c + 1] - edges[c])),
                                       Image.LANCZOS))
            remaining -= take
        course = np.concatenate([np.asarray(p, dtype=np.float32) for p in pieces], axis=1)
        course = np.asarray(Image.fromarray(course.astype(np.uint8))
                            .resize((width, int(edges[c + 1] - edges[c])), Image.LANCZOS),
                            dtype=np.float32)
        shifted = np.roll(course, (c % 2) * width // (2 * bricks), axis=1)
        out[edges[c]:edges[c + 1]] = shifted
    return out


# Town wall autotiles, one 64px tile per N/E/S/W connection mask (bit 1/2/4/8)
# and per (mx % 2, my % 2) so the masonry stays continuous in world space:
#   x = mask * 64, y = ((my % 2) * 2 + mx % 2) * 64
WALL_SIDE = 0.2      # vertical walls occupy x in [WALL_SIDE, 1 - WALL_SIDE]
WALL_TOP = 0.08      # horizontal wall walkway spans y in [WALL_TOP, WALL_FACE]
WALL_FACE = 0.42     # the south-facing masonry face runs from here to the tile bottom
WALL_PERIOD = 2


def build_walls():
    sheet = np.asarray(Image.open(SOURCE / "wall-textures.png").convert("RGB"), dtype=np.float32)
    half = sheet.shape[1] // 2
    face_h = TILE - int(round(WALL_FACE * TILE))
    face = masonry(sheet[:, :half], 2, 4, TILE * WALL_PERIOD, face_h)
    cap = masonry(sheet[:, half:], 5, 3, TILE * WALL_PERIOD, TILE * WALL_PERIOD)
    cap = cap * 0.92 + 12

    ys, xs = np.mgrid[0:TILE, 0:TILE].astype(np.float32) + 0.5
    u, v = xs / TILE, ys / TILE
    a, t, f = WALL_SIDE, WALL_TOP, WALL_FACE
    atlas = np.zeros((TILE * WALL_PERIOD * WALL_PERIOD, TILE * 16, 4), dtype=np.float32)

    def rect_sd(x0, x1, y0, y1):
        return np.minimum(np.minimum(u - x0, x1 - u), np.minimum(v - y0, y1 - v))

    for mask in range(16):
        n, e, s, w = (mask >> 0) & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1
        # arms overlap the centre and run past the tile so joins have no seams
        rects = [(a, 1 - a, t, f)]
        if n: rects.append((a, 1 - a, -1, f))
        if s: rects.append((a, 1 - a, t, 2))
        if e: rects.append((a, 2, t, f))
        if w: rects.append((-1, 1 - a, t, f))
        sd = np.max([rect_sd(*r) for r in rects], axis=0)
        cap_mask = sd > 0
        above = np.zeros_like(cap_mask)
        row = int(f * TILE) - 1
        above[:, :] = cap_mask[row][None, :]
        face_mask = (~cap_mask) & (v >= f) & above
        for py in range(WALL_PERIOD):
            for px in range(WALL_PERIOD):
                X = px + u
                Y = py + v
                rgb = cap[(py * TILE + ys.astype(int)) % cap.shape[0],
                          (px * TILE + xs.astype(int)) % cap.shape[1]].copy()
                # crenellated parapet rim around the walkway
                rim = (sd > 0) & (sd < 0.09)
                merlon = ((np.floor(X * 5) + np.floor(Y * 5)) % 2) == 0
                rgb = np.where((rim & merlon)[..., None], rgb * 1.12 + 10, rgb)
                rgb = np.where((rim & ~merlon)[..., None], rgb * 0.62, rgb)
                rgb = np.where((sd > 0)[..., None] & (sd < 2.5 / TILE)[..., None],
                               np.array([52, 40, 30], dtype=np.float32), rgb)
                fy = np.clip(ys.astype(int) - int(f * TILE), 0, face.shape[0] - 1)
                face_rgb = face[fy, (px * TILE + xs.astype(int)) % face.shape[1]]
                depth = (v - f) / (1 - f)
                face_rgb = face_rgb * (0.78 + 0.22 * smooth(0.0, 0.25, depth))[..., None]
                face_rgb = face_rgb * (1 - 0.35 * smooth(0.8, 1.0, depth))[..., None]
                rgb = np.where(face_mask[..., None], face_rgb, rgb)
                alpha = np.where(cap_mask | face_mask, 255.0, 0.0)
                # soft contact shadow just outside the walkway edges
                # the drop shadow is drawn at runtime from this silhouette (render.c)
                tile = np.dstack([rgb, alpha])
                y0 = (py * WALL_PERIOD + px) * TILE
                atlas[y0:y0 + TILE, mask * TILE:(mask + 1) * TILE] = tile
    return Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8))


# Shared lighting: light from the upper left, so every wall, building and
# object casts its shadow toward the lower right by the same offset.
SHADOW_DX = 0.12      # tiles
SHADOW_DY = 0.1       # tiles
SHADOW_ALPHA = 150
SHADOW_RGB = (8, 16, 10)   # render.c draws wall shadows with the same values


def key_sheet(path):
    sheet = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    r, g, b = sheet[..., 0], sheet[..., 1], sheet[..., 2]
    keyed = is_magenta(sheet) | ((np.minimum(r, b) - g > 55) & (np.abs(r - b) < 70))
    # outlines were antialiased into the magenta: drop purplish pixels near the key
    near = keyed.copy()
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            near |= np.roll(keyed, (dy, dx), axis=(0, 1))
    keyed |= near & (np.minimum(r, b) - g > 18) & (np.abs(r - b) < 90)
    return sheet, (~keyed).astype(np.float32)


def scaled_sprite(sheet, alpha_full, box, out_w, max_h):
    x0, y0, x1, y1 = box
    alpha = alpha_full[y0:y1, x0:x1]
    rgba = np.dstack([sheet[y0:y1, x0:x1] * alpha[..., None], alpha * 255])
    out_h = int(round(out_w * (y1 - y0) / (x1 - x0)))
    if out_h > max_h:
        out_w, out_h = int(round(out_w * max_h / out_h)), max_h
    img = Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8))
    arr = np.asarray(img.resize((out_w, out_h), Image.LANCZOS), dtype=np.float32)
    a = arr[..., 3:4] / 255
    arr[..., :3] = np.where(a > 0.01, arr[..., :3] / np.maximum(a, 0.01), 0)
    arr[..., 3] = np.where(arr[..., 3] < 60, 0, 255)
    return arr


def with_shadow(canvas, sprite, left, top, tile_px, footprint=False):
    """Composite `sprite` onto `canvas` over its lower-right drop shadow."""
    h, w = sprite.shape[:2]
    dx, dy = int(round(SHADOW_DX * tile_px)), int(round(SHADOW_DY * tile_px))
    shadow = np.zeros(canvas.shape[:2], dtype=np.float32)
    if footprint:
        # people and small props: a soft ellipse at their feet
        ys, xs = np.mgrid[0:canvas.shape[0], 0:canvas.shape[1]].astype(np.float32)
        cx, cy = left + w / 2 + dx, top + h - 0.04 * tile_px
        rx, ry = w * 0.36, 0.09 * tile_px
        shadow = np.clip(1.4 - np.hypot((xs - cx) / rx, (ys - cy) / ry), 0, 1)
    else:
        shadow[top + dy:top + dy + h, left + dx:left + dx + w] = sprite[..., 3] / 255
        blur = Image.fromarray((shadow * 255).astype(np.uint8))
        blur = blur.filter(ImageFilter.GaussianBlur(tile_px / 40))
        shadow = np.asarray(blur, dtype=np.float32) / 255
    base = canvas.copy()
    sa = shadow * SHADOW_ALPHA
    base[..., :3] = np.where(sa[..., None] > 0, np.array(SHADOW_RGB, dtype=np.float32),
                             base[..., :3])
    base[..., 3] = np.maximum(base[..., 3], sa)
    region = base[top:top + h, left:left + w]
    a = sprite[..., 3:4] / 255
    region[..., :3] = sprite[..., :3] * a + region[..., :3] * (1 - a)
    region[..., 3] = np.maximum(region[..., 3], sprite[..., 3])
    return base


# Town buildings: 384x384 cells (96x96 logical px) in a 3x3 grid. Each sprite is
# centred horizontally with its base BUILDING_BASE px above the cell bottom,
# leaving room for the shadow. Order matches the Building enum in render.c.
BUILDING_CELL = 384
BUILDING_BASE = 8
BUILDING_SOURCES = (
    # sheet, rows, sprites per row, width in tiles for each sprite
    ("buildings.png", 2, 3, (5.0, 5.0, 5.0, 5.0, 4.5, None)),   # cottage..gatehouse
    ("towers.png", 2, 2, (1.7, 1.7, 1.7, 1.7)),                 # NW, NE, SW, SE
)


def sprite_boxes(alpha, rows, per_row):
    """Find sprite bounding boxes by splitting each sheet row on empty columns."""
    boxes = []
    band = alpha.shape[0] // rows
    for r in range(rows):
        part = alpha[r * band:(r + 1) * band]
        cols = part.any(axis=0)
        runs, start = [], None
        for x, filled in enumerate(np.append(cols, False)):
            if filled and start is None:
                start = x
            elif not filled and start is not None:
                runs.append((start, x))
                start = None
        runs = sorted(sorted(runs, key=lambda s: s[1] - s[0])[-per_row:])
        for x0, x1 in runs:
            ys = np.nonzero(part[:, x0:x1].any(axis=1))[0]
            boxes.append((x0, r * band + ys.min(), x1, r * band + ys.max() + 1))
    return boxes


def build_buildings():
    atlas = np.zeros((BUILDING_CELL * 3, BUILDING_CELL * 3, 4), dtype=np.float32)
    index = 0
    for name, rows, per_row, widths in BUILDING_SOURCES:
        sheet, alpha = key_sheet(SOURCE / name)
        for box, tiles_wide in zip(sprite_boxes(alpha > 0, rows, per_row), widths):
            if tiles_wide is None:
                continue
            sprite = scaled_sprite(sheet, alpha, box, int(round(tiles_wide * TILE)),
                                   BUILDING_CELL - BUILDING_BASE)
            h, w = sprite.shape[:2]
            cell = np.zeros((BUILDING_CELL, BUILDING_CELL, 4), dtype=np.float32)
            cell = with_shadow(cell, sprite, (BUILDING_CELL - w) // 2,
                               BUILDING_CELL - BUILDING_BASE - h, TILE)
            y, x = (index // 3) * BUILDING_CELL, (index % 3) * BUILDING_CELL
            atlas[y:y + BUILDING_CELL, x:x + BUILDING_CELL] = cell
            index += 1
    return Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8))


# Order matches BattleScene in game.h. The second value is the source row that
# becomes the top of the 2:1 crop, placing each horizon ~60px below the top.
BATTLE_SCENES = (("grass", 130), ("forest", 150), ("road", 140), ("shore", 140), ("vault", 150))
BATTLE_SIZE = (1152, 576)


def build_battle():
    atlas = Image.new("RGB", (BATTLE_SIZE[0], BATTLE_SIZE[1] * len(BATTLE_SCENES)))
    for index, (name, top) in enumerate(BATTLE_SCENES):
        source = Image.open(SOURCE / f"battle-{name}.png").convert("RGB")
        height = source.width * BATTLE_SIZE[1] // BATTLE_SIZE[0]
        scene = source.crop((0, top, source.width, top + height))
        atlas.paste(scene.resize(BATTLE_SIZE, Image.LANCZOS), (0, index * BATTLE_SIZE[1]))
    return atlas


def main():
    textures = load_textures()
    Image.fromarray(np.clip(textures["grass"], 0, 255).astype(np.uint8)).save(OUT / "grass.png")
    build_terrain(textures).save(OUT / "terrain.png")
    build_objects().save(OUT / "objects.png")
    build_walls().save(OUT / "walls.png")
    build_buildings().save(OUT / "buildings.png")
    build_battle().save(OUT / "battle.png")
    print("wrote grass.png, terrain.png, objects.png, walls.png, buildings.png, battle.png to", OUT)


if __name__ == "__main__":
    main()
