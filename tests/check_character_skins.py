"""Integration regression for the model-14 client atlas/cutout preparation."""
import json
from pathlib import Path
import sys
from PIL import Image

root=Path(sys.argv[1])
character=json.loads((root/'character.json').read_text())
head=next(s for s in character['sections'] if s['part']==0)
hair=next(s for s in character['sections'] if s['part']==8)
assert Path(head['skinSource']).parent.name.lower()=='mskins', 'Legacy duplicate face atlas selected'
assert Path(hair['skinSource']).parent.name.lower()=='mskins', 'Legacy duplicate hair atlas selected'
assert '_dkbrown' in hair['skin'].lower(), 'Finished color-table hair skin was not selected'
assert hair['alphaFlags'] & 512, 'Hair/beard cutouts imported opaque'
with Image.open(root/hair['texturePng']) as texture:
    assert texture.getpixel((255,0))[3]==0, 'Unused hair atlas region must be transparent'
assert len(character['bones'])==129 and len(character['sections'])==8
print('Modern face atlas, finished hair palette, cutout alpha and unchanged skeleton passed')
