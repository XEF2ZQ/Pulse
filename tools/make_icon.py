"""Render Pulse's original code-native icon at Windows icon sizes."""
from pathlib import Path
from PIL import Image, ImageDraw
folder = Path(__file__).resolve().parent.parent / 'src'
size = 1024
im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rounded_rectangle((20, 20, 1004, 1004), radius=230, fill='#142331')
d.rounded_rectangle((38, 38, 986, 986), radius=216, outline='#315748', width=10)
d.arc((180, 178, 844, 844), 140, 408, fill='#7be7c7', width=56)
points = [(190, 543), (364, 543), (447, 367), (552, 668), (655, 464), (821, 464)]
d.line(points, fill='#7be7c7', width=59, joint='curve')
for x,y in points:
    d.ellipse((x-29,y-29,x+29,y+29), fill='#7be7c7')
d.ellipse((754, 207, 858, 311), fill='#ffc770')
im.save(folder/'pulse.ico', sizes=[(16,16),(20,20),(24,24),(32,32),(40,40),(48,48),(64,64),(128,128),(256,256)])
im.resize((256,256),Image.Resampling.LANCZOS).save(folder/'pulse.png')
(folder/'pulse.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024"><rect x="20" y="20" width="984" height="984" rx="230" fill="#142331"/><rect x="38" y="38" width="948" height="948" rx="216" fill="none" stroke="#315748" stroke-width="10"/><path d="M 258 726 A 332 332 0 1 1 758 735" fill="none" stroke="#7be7c7" stroke-width="56"/><path d="M190 543H364L447 367L552 668L655 464H821" fill="none" stroke="#7be7c7" stroke-width="59" stroke-linecap="round" stroke-linejoin="round"/><circle cx="806" cy="259" r="52" fill="#ffc770"/></svg>''',encoding='utf-8')
