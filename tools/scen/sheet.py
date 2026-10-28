"""Contact sheet of a scenario's screenshots (labelled)."""
import glob
import os
import sys

from PIL import Image, ImageDraw


def sheet(d, out, cols=4, scale=0.4, crop=(0, 100, 794, 550)):
    files = sorted(glob.glob(os.path.join(d, "*.ppm")))
    ims = []
    for f in files:
        im = Image.open(f).crop(crop)
        im = im.resize((int(im.width * scale), int(im.height * scale)))
        ImageDraw.Draw(im).text((3, 3), os.path.basename(f)[:-4], fill=(255, 255, 0))
        ims.append(im)
    if not ims:
        return
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    g = Image.new("RGB", (cols * w, rows * h))
    for i, im in enumerate(ims):
        g.paste(im, ((i % cols) * w, (i // cols) * h))
    g.save(out)


if __name__ == "__main__":
    sheet(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 4)
