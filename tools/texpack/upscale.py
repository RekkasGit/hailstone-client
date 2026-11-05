"""Upscales a texpack export (tools/texpack.cpp) and generates its normal and roughness maps on the GPU.

    python -I upscale.py WORKDIR MODELDIR [--model NAME] [--out up] [--limit N] [--only-hash H,...] [--no-maps]
                         [--redo-alpha STAMP]

Reads WORKDIR/manifest.jsonl and WORKDIR/src/<hash>[.m|.k].png and writes WORKDIR/<out>/<hash>.png (colour, 2x),
<hash>.m.png (the masked variant), <hash>.k.png (the variant without its colour key), <hash>.n.png (normal, tangent space, +Y up) and <hash>.r.png (roughness, grey).
Outputs that exist are skipped, so a stopped run resumes. Models load through spandrel (PyTorch, ROCm or CUDA):

  colour   4x-PBRify_UpscalerSPANV4.pth by default, its 4x result taken down to 2x (about 4 textures a second on an
           RX 9060 XT); 4x-PBRify-UpscalerV4.safetensors (DAT) is a little sharper and ten times slower; any spandrel
           model works (--model), at any scale
  normal   1x-PBRify_NormalV3.pth
  rough    1x-PBRify_RoughnessV2.pth

Textures the models would only damage are scaled with Lanczos and get no maps: tiny ones, the game's own normal
maps, and the EQG terrain's coverage, palette and mask maps.
"""

import argparse
import json
import math
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import torch
from PIL import Image

import spandrel

SCALE = 2
PAD = 16  # texels of the source added round each edge before a model, cropped after
TILE = 384  # source texels per model call; bigger images go in overlapping tiles

# Names (without extension) that are data, not pictures: EQG terrain blend maps and the like.
DATA_NAMES = re.compile(r"(coverage|texturepalette|_mask|mask_|lightmap|_lm$|^fx_|_nm$|_nrm$|_normal|_n$|_bump)")


def log(*args):
    print(*args, flush=True)


def load_model(path, device):
    model = spandrel.ModelLoader(device=device).load_from_file(str(path))
    model.eval()
    half = model.supports_half and device.type == "cuda"
    if half:
        model.half()
    return model, half


def pad(img, mode):
    """img: HxWxC float32. Wrap for tiling world textures, reflect for character skins."""
    p = min(PAD, img.shape[0], img.shape[1])
    return np.pad(img, ((p, p), (p, p), (0, 0)), mode="wrap" if mode == "wrap" else "reflect"), p


@torch.inference_mode()
def run_model(model, half, img, device):
    """img: HxWx3 float32 0..1 -> (H*s)x(W*s)x3, in overlapping tiles."""
    s = model.scale
    h, w, _ = img.shape
    out = np.zeros((h * s, w * s, 3), np.float32)
    weight = np.zeros((h * s, w * s, 1), np.float32)
    overlap = 16
    step = TILE - 2 * overlap
    ys = list(range(0, max(h - 2 * overlap, 1), step)) if h > TILE else [0]
    xs = list(range(0, max(w - 2 * overlap, 1), step)) if w > TILE else [0]
    for y0 in ys:
        for x0 in xs:
            y1, x1 = min(y0 + TILE, h), min(x0 + TILE, w)
            y0c, x0c = max(0, y1 - TILE), max(0, x1 - TILE)
            tile = img[y0c:y1, x0c:x1]
            t = torch.from_numpy(np.ascontiguousarray(tile.transpose(2, 0, 1)))[None].to(device)
            t = t.half() if half else t.float()
            r = model(t).float().clamp_(0, 1)[0].permute(1, 2, 0).cpu().numpy()
            # Feathered weights so tile seams blend.
            th, tw = r.shape[:2]
            wy = np.minimum(np.arange(th) + 1, np.arange(th)[::-1] + 1).clip(max=overlap * s)
            wx = np.minimum(np.arange(tw) + 1, np.arange(tw)[::-1] + 1).clip(max=overlap * s)
            wt = (wy[:, None] * wx[None, :]).astype(np.float32)[..., None]
            out[y0c * s:y1 * s, x0c * s:x1 * s] += r * wt
            weight[y0c * s:y1 * s, x0c * s:x1 * s] += wt
    return out / np.maximum(weight, 1e-6)


def bleed(rgb, alpha):
    """Colour of transparent texels grown in from opaque neighbours, so a model never sees the key colour."""
    known = alpha > 0
    if known.all() or not known.any():
        return rgb
    rgb = rgb.copy()
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        n = np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(known, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * k[..., None]
            n += k
        grow = (~known) & (n > 0)
        rgb[grow] = acc[grow] / n[grow][:, None]
        known = known | grow
    rgb[~known] = rgb[known].mean(0)
    return rgb


def lanczos(img, w, h):
    """img: HxWxC float32 -> hxwxC, channel by channel through PIL."""
    chans = [np.asarray(Image.fromarray(img[..., c].astype(np.float32), "F").resize((w, h), Image.LANCZOS))
             for c in range(img.shape[2])]
    return np.clip(np.stack(chans, -1), 0, 1)


def upscale_rgb(model, half, rgb, edge, device):
    h, w, _ = rgb.shape
    padded, p = pad(rgb, edge)
    up = run_model(model, half, padded, device)
    s = model.scale
    up = up[p * s:(p + h) * s, p * s:(p + w) * s]
    if s != SCALE:
        up = lanczos(up, w * SCALE, h * SCALE)
    return up


def upscale_alpha(alpha, kind, w, h):
    a = lanczos(alpha[..., None], w, h)[..., 0]
    if kind == "cutout":
        a = (a >= 0.5).astype(np.float32)
    return a


def map_1x(model, half, rgb, edge, device):
    h, w, _ = rgb.shape
    padded, p = pad(rgb, edge)
    return run_model(model, half, padded, device)[p:p + h, p:p + w]


def save(path, arr):
    arr = (np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8)
    tmp = path.with_suffix(".tmp.png")
    Image.fromarray(arr, "RGBA" if arr.ndim == 3 and arr.shape[2] == 4 else ("L" if arr.ndim == 2 else "RGB")).save(tmp, compress_level=1)
    tmp.replace(path)


def read(path):
    return np.asarray(Image.open(path).convert("RGBA"), np.float32) / 255.0


def is_data(entry):
    stored = entry["stored"]
    if "Q8W8V8U8" in stored or "CxV8U8" in stored:
        return True
    return any(DATA_NAMES.search(Path(n).stem) for n in entry["names"])


def edge_mode(entry):
    return "reflect" if all("_chr" in a or a.startswith("chequip") for a in entry["archives"]) else "wrap"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work", type=Path)
    ap.add_argument("models", type=Path)
    ap.add_argument("--model", default="4x-PBRify_UpscalerSPANV4.pth")
    ap.add_argument("--out", default="up")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only-hash", default="")
    ap.add_argument("--no-maps", action="store_true")
    # Textures with an alpha channel whose outputs are older than the file STAMP are made again (for a work folder
    # upscaled before their colour under alpha 0 was kept): touch STAMP once, then run (and resume) with it.
    ap.add_argument("--redo-alpha", type=Path, metavar="STAMP")
    args = ap.parse_args()
    redo_before = args.redo_alpha.stat().st_mtime if args.redo_alpha else None

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if device.type != "cuda":
        log("warning: no GPU found by PyTorch; running on the CPU (very slow)")
    up_model, up_half = load_model(args.models / args.model, device)
    if not args.no_maps:
        n_model, n_half = load_model(args.models / "1x-PBRify_NormalV3.pth", device)
        r_model, r_half = load_model(args.models / "1x-PBRify_RoughnessV2.pth", device)
    out = args.work / args.out
    out.mkdir(parents=True, exist_ok=True)

    entries = [json.loads(line) for line in open(args.work / "manifest.jsonl")]
    if args.only_hash:
        wanted = set(args.only_hash.split(","))
        entries = [e for e in entries if e["hash"] in wanted]
    if args.limit:
        entries = entries[:args.limit]

    def todo(e):
        names = [f"{e['hash']}.png"]
        if e["masked_variant"]:
            names.append(f"{e['hash']}.m.png")
        if e.get("keyed_variant"):
            names.append(f"{e['hash']}.k.png")
        if not args.no_maps and not is_data(e) and max(e["w"], e["h"]) > 16:
            names += [f"{e['hash']}.n.png", f"{e['hash']}.r.png"]
        redo = redo_before is not None and e["alpha"] != "none"

        def missing(n):
            p = out / n
            return not p.exists() or (redo and not n.endswith((".m.png", ".k.png")) and p.stat().st_mtime < redo_before)

        return [n for n in names if missing(n)]

    work = [(e, todo(e)) for e in entries]
    work = [(e, t) for e, t in work if t]
    log(f"{len(entries)} textures, {len(work)} to do, model {args.model} on {device}")
    io = ThreadPoolExecutor(8)
    failures = 0
    start = time.time()
    for i, (e, missing) in enumerate(work):
        h = e["hash"]
        try:
            src = read(args.work / "src" / f"{h}.png")
            sh, sw = src.shape[:2]
            W, H = sw * SCALE, sh * SCALE
            edge = edge_mode(e)
            plain_ml = not is_data(e) and max(sw, sh) > 16

            def colour(img, kind, keyed):
                # Only a colour key (the masked variant's palette index 0) hides nothing worth keeping. A DDS's
                # alpha 0 keeps the picture an opaque material draws (the hero's forge plate generic_plate_blue_c.dds
                # holds all but its cloth under alpha 0), so its colour goes to the model as stored.
                rgb, alpha = img[..., :3], img[..., 3]
                if keyed and kind != "none":
                    rgb = bleed(rgb, alpha)
                rgb = upscale_rgb(up_model, up_half, rgb, edge, device) if plain_ml else lanczos(rgb, W, H)
                a = upscale_alpha(alpha, kind, W, H) if kind != "none" else np.ones((H, W), np.float32)
                return rgb, np.concatenate([rgb, a[..., None]], -1)

            rgb, rgba = colour(src, e["alpha"], False)
            if f"{h}.png" in missing:
                io.submit(save, out / f"{h}.png", rgba)
            if f"{h}.m.png" in missing:
                m = read(args.work / "src" / f"{h}.m.png")
                kind = "cutout" if e["alpha"] in ("none", "cutout") else "blend"
                io.submit(save, out / f"{h}.m.png", colour(m, kind, True)[1])
            if f"{h}.k.png" in missing:
                # The colour key already taken out (texpack export): its clear texels hold the leaves' colour.
                k = read(args.work / "src" / f"{h}.k.png")
                io.submit(save, out / f"{h}.k.png", colour(k, e["alpha"], True)[1])
            if f"{h}.n.png" in missing:
                io.submit(save, out / f"{h}.n.png", map_1x(n_model, n_half, rgb, edge, device))
            if f"{h}.r.png" in missing:
                io.submit(save, out / f"{h}.r.png", map_1x(r_model, r_half, rgb, edge, device)[..., 0])
        except Exception as ex:  # one bad texture must not stop a night's run
            failures += 1
            log(f"{h} {e['names'][:3]}: {ex}")
        if (i + 1) % 100 == 0 or i + 1 == len(work):
            rate = (i + 1) / (time.time() - start)
            left = (len(work) - i - 1) / rate
            log(f"{i + 1}/{len(work)}  {rate:.2f}/s  about {left / 3600:.1f} h left")
    io.shutdown(wait=True)
    log(f"done, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
