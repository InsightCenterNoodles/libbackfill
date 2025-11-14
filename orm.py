#!/usr/bin/env python3
"""
Build an ORM texture from separate Metalness, Roughness, and optional Occlusion maps.

Default packing:
  R = Ambient Occlusion (AO)
  G = Roughness
  B = Metalness

Examples
--------
python make_orm.py \
  --metal metal.png \
  --rough rough.png \
  --occlusion ao.png \
  --out orm.png

# If your roughness is actually glossiness (needs invert):
python make_orm.py --metal metal.png --rough gloss.png --invert-rough --out orm.png

# No AO provided (fills R with 255):
python make_orm.py --metal metal.png --rough rough.png --out orm.png
"""

import argparse
from PIL import Image

def to_gray_8(img: Image.Image) -> Image.Image:
    """Convert any image to single-channel 8-bit grayscale."""
    if img.mode not in ("L", "I;16", "I", "F"):
        img = img.convert("L")
    else:
        # Convert higher bit depth to 8-bit with simple normalization
        img = img.point(lambda x: x * (255.0 / 65535.0) if img.mode == "I;16" else x)
        img = img.convert("L")
    return img

def maybe_invert(img: Image.Image, do_invert: bool) -> Image.Image:
    return img.point(lambda p: 255 - p) if do_invert else img

def load_and_prep(path: str, size=None, invert=False, resize_filter=Image.BICUBIC) -> Image.Image:
    img = Image.open(path)
    img = to_gray_8(img)
    if size is not None and img.size != size:
        img = img.resize(size, resize_filter)
    img = maybe_invert(img, invert)
    return img

def main():
    p = argparse.ArgumentParser(description="Create an ORM texture (R=AO, G=Roughness, B=Metalness).")
    p.add_argument("--metal", required=True, help="Metalness map (grayscale image).")
    p.add_argument("--rough", required=True, help="Roughness map (grayscale image).")
    p.add_argument("--occlusion", help="Ambient Occlusion map (grayscale image). Optional.")
    p.add_argument("--out", required=True, help="Output file (e.g., orm.png).")

    # Channel options
    p.add_argument("--invert-metal", action="store_true", help="Invert metalness channel.")
    p.add_argument("--invert-rough", action="store_true", help="Invert roughness channel (e.g., if you have gloss).")
    p.add_argument("--invert-occlusion", action="store_true", help="Invert occlusion channel.")

    # Fill/defaults and resizing
    p.add_argument("--ao-fill", type=int, default=255, help="Value for AO if not provided (0–255). Default 255.")
    p.add_argument("--resize-ref", choices=["metal", "rough", "occlusion"], default="metal",
                   help="Which image defines the output resolution. Default: metal.")
    p.add_argument("--resize-filter", choices=["nearest", "bilinear", "bicubic", "lanczos"],
                   default="bicubic", help="Resample filter for resizing. Default: bicubic.")

    # Optional alpha packing (disabled by default)
    p.add_argument("--alpha-from", choices=["none", "metal", "rough", "occlusion"], default="none",
                   help="Optionally store one input into alpha channel. Default: none.")

    args = p.parse_args()

    # Map filter name to Pillow filter
    filt_map = {
        "nearest": Image.NEAREST,
        "bilinear": Image.BILINEAR,
        "bicubic": Image.BICUBIC,
        "lanczos": Image.LANCZOS
    }
    filt = filt_map[args.resize_filter]

    # Load reference image to set output size
    ref_path = getattr(args, args.resize_ref)
    if ref_path is None:
        # If occlusion chosen but missing, fall back to metal
        ref_path = args.metal
    ref_img = Image.open(ref_path)
    out_size = ref_img.size

    # Load channels
    metal = load_and_prep(args.metal, out_size, args.invert_metal, filt)
    rough = load_and_prep(args.rough, out_size, args.invert_rough, filt)

    if args.occlusion:
        ao = load_and_prep(args.occlusion, out_size, args.invert_occlusion, filt)
    else:
        # Create solid AO if none provided
        val = max(0, min(255, args.ao_fill))
        ao = Image.new("L", out_size, color=val)

    # Compose RGB (R=AO, G=Roughness, B=Metalness)
    orm_rgb = Image.merge("RGB", (ao, rough, metal))

    # Optional Alpha channel
    alpha = None
    if args.alpha_from != "none":
        src = {
            "metal": metal,
            "rough": rough,
            "occlusion": ao
        }.get(args.alpha_from)
        if src is not None:
            alpha = src
    if alpha is not None:
        out_img = Image.merge("RGBA", (*orm_rgb.split(), alpha))
    else:
        out_img = orm_rgb

    # Save (PNG recommended for lossless single 8-bit channels)
    out_img.save(args.out)
    print(f"Saved ORM to: {args.out}  (R=AO, G=Roughness, B=Metalness{', A='+args.alpha_from if alpha is not None else ''})")

if __name__ == "__main__":
    main()
