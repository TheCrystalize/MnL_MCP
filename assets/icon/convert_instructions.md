Commands to convert assets/icon/MnLLogo.png into a multi-resolution .ico (run from project root c:\dev\MnL_MCP)

ImageMagick (v7)
```bash
magick assets/icon/MnLLogo.png -define icon:auto-resize=256,128,64,48,32,16 assets/icon/MnLLogo.ico
```

ImageMagick (v6)
```bash
convert assets/icon/MnLLogo.png -define icon:auto-resize=256,128,64,48,32,16 assets/icon/MnLLogo.ico
```

Python + Pillow
```python
from PIL import Image
sizes = [256,128,64,48,32,16]
im = Image.open('assets/icon/MnLLogo.png').convert('RGBA')
im.save('assets/icon/MnLLogo.ico', sizes=[(s,s) for s in sizes])