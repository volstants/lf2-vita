"""Convert original background BMPs without basename collisions; preserve pixels."""
from pathlib import Path
import hashlib,json
from PIL import Image
from bmp2png import load_bmp
ROOT=Path(__file__).resolve().parents[1]
records=[]
for source in sorted((ROOT/'bg'/'sys').glob('*/*.bmp')):
    relative=source.relative_to(ROOT)
    dest=ROOT/'assets'/('_'.join(relative.with_suffix('').parts)+'.png')
    decoder='Pillow'
    try:
        im=Image.open(source);im.load()
    except (ValueError,OSError):
        im=load_bmp(source);decoder='LF2 RLE8 compatibility decoder'
    with im:
        im.save(dest)
        with Image.open(dest) as result:
            assert im.convert('RGB').tobytes()==result.convert('RGB').tobytes(),source
        records.append({'source':relative.as_posix(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'png':dest.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
            'size':im.size,'decoder':decoder})
out=ROOT/'reference'/'round-interface-2026-09-22'/'background-assets.json'
out.write_text(json.dumps(records,indent=2),encoding='utf-8')
print(f'{len(records)} original BMPs converted, all pixels verified')
