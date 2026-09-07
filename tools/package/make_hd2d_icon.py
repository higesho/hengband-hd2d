# -*- coding: utf-8 -*-
"""Windows 版の画面（HengbandHd2d.exe）のアイコンを作る。
2026-09-06 に決めた「配布用の HengbandHd2d.exe のアイコンをアンドロイドと同じ剣のアイコンに」。

Android の起動アイコン（`android/hd2d/src/main/res/drawable/ic_launcher_foreground.xml` と
`values/ic_launcher_background.xml`）と**同じ形・同じ色**を、そのままの座標（108 四方）で描く。
向こうはベクタなので、こちらは 4 倍で描いて縮める（角の階段を消す）。
"""
from PIL import Image, ImageDraw

import os
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                   'platform', 'windows', 'hd2d.ico')
S = 108           # Android のビューポート
SS = 8            # 4 倍でも足りないので 8 倍で描いて縮める
BG = (0x10, 0x1A, 0x2E, 255)      # ic_launcher_background
BLADE = (0xF0, 0xDC, 0xA8, 255)
RIDGE = (0xB9, 0xA4, 0x73, 255)
GOLD = (0xD9, 0xA4, 0x41, 255)
GRIP = (0x7A, 0x4A, 0x2B, 255)


def scaled(points):
    return [(x * SS, y * SS) for (x, y) in points]


img = Image.new('RGBA', (S * SS, S * SS), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
#! 背景。Windows のアイコンは丸くくり抜かれないので、角を丸めた四角にする。
d.rounded_rectangle([0, 0, (S * SS) - 1, (S * SS) - 1], radius=18 * SS, fill=BG)
#! 刃
d.polygon(scaled([(54, 18), (60, 29), (60, 63), (48, 63), (48, 29)]), fill=BLADE)
#! 刃の稜線（右半分だけ暗い）
d.polygon(scaled([(54, 18), (60, 29), (60, 63), (54, 63)]), fill=RIDGE)
#! 鍔
d.rectangle(scaled([(36, 63), (72, 70)]), fill=GOLD)
#! 柄
d.rectangle(scaled([(50, 70), (58, 84)]), fill=GRIP)
#! 柄頭
d.ellipse(scaled([(48, 78), (60, 90)]), fill=GOLD)

sizes = (256, 128, 64, 48, 32, 16)
frames = [img.resize((n, n), Image.LANCZOS) for n in sizes]
frames[0].save(OUT, format='ICO', sizes=[(n, n) for n in sizes])
print('できました:', OUT)
