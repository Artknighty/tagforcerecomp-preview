#!/usr/bin/env python3
"""Regroups the Enhanced Textures package by theme (cards, characters, menus, duel, deck, text, world, intro, extras).

Before: ~120 folders (the MikeyMK tree plus one `*_hd` folder per redraw tool). After: 9 top-level themes.
The runtime finds every PNG through textures.ini / textures_anime.ini, so only those files, origem.tsv and
texturas_recentes.txt are rewritten. The BC7 disk cache key contains the path relative to `textures/`, so the cache
files are renamed too (otherwise ~7 GB would be recompressed on the next run).

  python tools/mods/reorganizar_texturas.py            # dry run: prints the plan
  python tools/mods/reorganizar_texturas.py --apply    # moves files, rewrites the indexes, renames the cache
  python tools/mods/reorganizar_texturas.py --revert   # undoes --apply from the saved manifest

Close the game first. The manifest (old<TAB>new) is saved next to the package as layout_manifest.tsv.
"""
import argparse
import os
import sys
from collections import Counter

try:
    import xxhash
except ImportError:
    xxhash = None

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MOD = os.path.join(ROOT, "mods", "EnhancedTextures")
TEX = os.path.join(MOD, "textures")
CACHE = os.path.join(ROOT, "texcache")
MANIFEST = os.path.join(MOD, "layout_manifest.tsv")
INDEXES = [os.path.join(TEX, "textures.ini"), os.path.join(TEX, "textures_anime.ini"), os.path.join(MOD, "origem.tsv"),
           os.path.join(ROOT, "texturas_recentes.txt")]

# (old prefix, new folder, flatten, prefix added to the file name). Longest matching old prefix wins.
# flatten: sub-folders below the old prefix are folded into the file name ("win/e.png" -> "win__e.png").
RULES = [
    ("mikeymk/cards/frame", "cards/frame", False, ""),
    ("mikeymk/cards/large", "cards/large", False, ""),
    ("mikeymk/cards/smallANIME", "cards/small_anime", False, ""),
    ("mikeymk/cards/small", "cards/small", False, ""),
    ("mikeymk/cards/tinyANIME", "cards/tiny_anime", False, ""),
    ("mikeymk/cards/tiny", "cards/tiny", False, ""),
    ("mikeymk/cards", "cards", False, ""),
    ("cartas_eu", "cards/small_eu", True, ""),
    ("mikeymk/characters/3d", "characters/3d", False, ""),
    ("mikeymk/characters/combinations", "characters/parts", True, ""),
    ("mikeymk/characters/portraits", "characters/portraits", True, ""),
    ("mikeymk/characters/sprites", "characters/sprites", False, ""),
    ("mikeymk/events", "world/events", False, ""),
    ("mikeymk/map", "world/map", True, ""),
    ("mikeymk/shop", "world/shop", True, ""),
    ("mikeymk/extras/iso", "extras/iso", False, ""),
    ("mikeymk/extras", "extras", False, ""),
    ("mikeymk/intro", "intro", True, ""),
    ("intro/pc", "intro/pc", False, ""),
    ("mikeymk/ptbr", "text/ptbr", False, ""),
    ("mikeymk/ui/text", "text", True, ""),
    ("mikeymk/ui/cardalbum", "deck", True, "album__"),
    ("mikeymk/ui/deckedit", "deck", True, "deckedit__"),
    ("deck/pc", "deck/pc", False, ""),
    ("mikeymk/ui/duels", "duel", True, ""),
    ("duel/pc", "duel/pc", False, ""),
    ("duel/pc", "duel/pc", False, ""),
    ("duel/pc", "duel/pc", False, ""),
    ("mikeymk/ui/menu", "menus", True, ""),
    ("mikeymk/ui/misc", "menus", True, "misc__"),
    ("mikeymk/ui/oval", "menus", True, "oval__"),
    ("mikeymk/ui/loading", "menus", True, "loading__"),
    ("mikeymk/ui", "menus", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("menus/pc", "menus/pc", False, ""),
    ("xbox", "menus/pc", False, ""),
]
RULES.sort(key=lambda r: -len(r[0]))


def map_one(rel):
    """rel = path relative to textures/ with forward slashes. Returns the new relative path or None."""
    for old, new, flat, pre in RULES:
        if rel.startswith(old + "/"):
            rest = rel[len(old) + 1:]
            d, _, name = rest.rpartition("/")
            if flat and d:
                name = d.replace("/", "_") + "__" + name
            elif d:
                new = new + "/" + d
            return new + "/" + pre + name
    return None


def referenced():
    """Paths named by textures.ini / textures_anime.ini (the only PNGs the runtime ever opens)."""
    ref = set()
    for ix in INDEXES[:2]:
        for ln in open(ix, encoding="utf-8"):
            k, sep, v = ln.rstrip("\r\n").partition(" = ")
            if sep and not ln.startswith("["):
                ref.add(v)
    return ref


def list_pngs():
    out = []
    for dp, _, fs in os.walk(TEX):
        for f in fs:
            if f.lower().endswith(".png"):
                out.append(os.path.relpath(os.path.join(dp, f), TEX).replace(os.sep, "/"))
    return sorted(out)


def cache_name(rel, size, mtime_ns):
    """Mirror of disk_key() in runtime/gpu/texrepl.cpp: xxh64(seed 0x7E8C4C) of "<rel lower, \\ separators>|size|filetime"."""
    ft = mtime_ns // 100 + 116444736000000000
    key = "%s|%d|%d" % (rel.lower().replace("/", "\\"), size, ft)
    return "%016x.bc7" % xxhash.xxh64_intdigest(key.encode("utf-8"), 0x7E8C4C)


def rewrite_indexes(mapping):
    """mapping = {old_rel: new_rel}. Rewrites every index row that names an old path (whole-token match)."""
    for ix in INDEXES:
        if not os.path.exists(ix):
            continue
        with open(ix, "rb") as f:
            raw = f.read().decode("utf-8")
        n = 0
        lines = raw.split("\n")
        for i, ln in enumerate(lines):
            if ix.endswith("texturas_recentes.txt"):
                head, sep, tail = ln.partition("\\textures\\")
                if sep:
                    rel = tail.rstrip("\r").replace("\\", "/")
                    if rel in mapping:
                        lines[i] = head + sep + mapping[rel].replace("/", "\\") + ("\r" if ln.endswith("\r") else "")
                        n += 1
            elif ix.endswith(".ini"):
                k, sep, v = ln.partition(" = ")
                if sep and v.rstrip("\r") in mapping:
                    lines[i] = k + sep + mapping[v.rstrip("\r")] + ("\r" if ln.endswith("\r") else "")
                    n += 1
            else:
                cols = ln.rstrip("\r").split("\t")
                if len(cols) >= 4 and cols[3] in mapping:
                    cols[3] = mapping[cols[3]]
                    lines[i] = "\t".join(cols) + ("\r" if ln.endswith("\r") else "")
                    n += 1
        if not os.path.exists(ix + ".bak_layout"):
            with open(ix + ".bak_layout", "wb") as f:
                f.write(raw.encode("utf-8"))
        with open(ix, "wb") as f:
            f.write("\n".join(lines).encode("utf-8"))
        print("  %-28s %6d rows rewritten" % (os.path.basename(ix), n))


def prune_empty(base):
    for dp, dn, fn in os.walk(base, topdown=False):
        if dp != base and not os.listdir(dp):
            os.rmdir(dp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--revert", action="store_true")
    a = ap.parse_args()

    if a.revert:
        pairs = [ln.rstrip("\n").split("\t") for ln in open(MANIFEST, encoding="utf-8")]
        mapping = {n: o for o, n in pairs}
        for o, n in pairs:
            os.makedirs(os.path.dirname(os.path.join(TEX, o)), exist_ok=True)
            os.rename(os.path.join(TEX, n), os.path.join(TEX, o))
        rewrite_indexes(mapping)
        prune_empty(TEX)
        print("reverted %d files (cache files keep their new names: they will be recompressed)" % len(pairs))
        return

    files = list_pngs()
    plan, bad = {}, []
    ref = referenced()
    for rel in files:
        new = map_one(rel) if rel in ref else "_unreferenced/" + rel.replace("/", "__")   # superseded by a redraw
        if new is None:
            bad.append(rel)
        else:
            plan[rel] = new
    if bad:
        print("UNMAPPED:", *bad[:20], sep="\n  ")
        sys.exit(1)
    dup = [p for p, c in Counter(plan.values()).items() if c > 1]
    if dup:
        print("COLLISIONS:", *dup[:20], sep="\n  ")
        sys.exit(1)
    moves = {o: n for o, n in plan.items() if o != n}
    tops = Counter(n.split("/")[0] for n in plan.values())
    folders = {n.rpartition("/")[0] for n in plan.values()}
    print("%d PNGs, %d to move; %d folders after; top level: %s" % (len(files), len(moves), len(folders), dict(tops)))
    if not a.apply:
        return
    if xxhash is None:
        sys.exit("pip install xxhash (needed to rename the BC7 cache)")
    if os.path.exists(MANIFEST):
        sys.exit("layout_manifest.tsv already exists: run --revert first or delete it")

    renames, hits = [], 0
    for o, n in moves.items():
        st = os.stat(os.path.join(TEX, o))
        oc, nc = cache_name(o, st.st_size, st.st_mtime_ns), cache_name(n, st.st_size, st.st_mtime_ns)
        if os.path.exists(os.path.join(CACHE, oc)):
            hits += 1
            renames.append((oc, nc))
    print("cache: %d of %d moved files have a BC7 entry to carry over" % (hits, len(moves)))

    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        for o, n in moves.items():
            f.write("%s\t%s\n" % (o, n))
    for o, n in moves.items():
        dst = os.path.join(TEX, n)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.rename(os.path.join(TEX, o), dst)
    rewrite_indexes(moves)
    for oc, nc in renames:
        os.rename(os.path.join(CACHE, oc), os.path.join(CACHE, nc))
    prune_empty(TEX)
    print("done. Manifest: %s" % MANIFEST)


if __name__ == "__main__":
    main()
