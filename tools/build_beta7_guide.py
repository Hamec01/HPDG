"""Reproducible English Beta 7 guide and release copy. Run from repository root."""
from pathlib import Path
import math
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor, Color

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/pdf'
OUT.mkdir(parents=True, exist_ok=True)
DOC = ROOT / 'docs/beta7'
DOC.mkdir(parents=True, exist_ok=True)
for name, file in [('Body','arial.ttf'), ('Bold','arialbd.ttf')]:
    pdfmetrics.registerFont(TTFont(name, 'C:/Windows/Fonts/'+file))
pdfmetrics.registerFontFamily('Body',normal='Body',bold='Bold',italic='Body',boldItalic='Bold')
W,H = 595.28,841.89
BG=HexColor('#10151b'); FG=HexColor('#edf0ed'); MUT=HexColor('#aebbc4')
RED=HexColor('#f16e65'); GOLD=HexColor('#e9b862'); PANEL=HexColor('#1b242d')
pdf=OUT/'HPDG_Beta7_Guide_EN.pdf'
c=canvas.Canvas(str(pdf),pagesize=(W,H))
c.setTitle('HPDG Beta 7 | Update and User Guide')
c.setAuthor('HamloProd')
page=0
def p(text,x,y,width=495,size=11,color=FG,leading=None,bold=False):
    style=ParagraphStyle('p',fontName='Bold' if bold else 'Body',fontSize=size,leading=leading or size*1.48,textColor=color)
    para=Paragraph(text,style); _,height=para.wrap(width,H)
    if y-height < 48 and y > 46:
        raise ValueError(f'Page {page}: text overflow at {y-height}: {text[:55]}')
    para.drawOn(c,x,y-height)
    return y-height
def line(y):
    c.setStrokeColor(HexColor('#34414c'));c.setLineWidth(.6);c.line(48,y,W-48,y)
def start(kicker,title,subtitle=''):
    global page
    if page: c.showPage()
    page+=1;c.setFillColor(BG);c.rect(0,0,W,H,fill=1,stroke=0)
    c.setFillColor(RED);c.rect(0,H-9,W,9,fill=1,stroke=0)
    p('HAMLOPROD  /  HPDG',48,H-37,size=9,color=MUT)
    p(kicker.upper(),48,H-86,size=9,color=GOLD,bold=True)
    y=p(title,48,H-111,size=29,bold=True,leading=33)
    if subtitle: y=p(subtitle,48,y-14,size=11,color=MUT)
    line(46);p('HPDG 0.1.0-beta.7  |  Windows x64 / VST3',48,33,size=8,color=MUT)
    c.setFillColor(GOLD);c.setFont('Bold',9);c.drawRightString(W-48,21,f'{page:02}')
    return y-27
def block(title,text,y):
    y=p(title,48,y,size=14,color=GOLD,bold=True)
    return p(text,48,y-8,size=11)-20
def formula(text,caption,y):
    c.setFillColor(PANEL);c.roundRect(48,y-74,W-96,74,8,fill=1,stroke=0)
    p(text,63,y-12,width=W-126,size=15,color=GOLD,bold=True)
    p(caption,63,y-42,width=W-126,size=9,color=MUT)
    return y-93
def grid(y,rows):
    x0=129;cell=24;rh=30
    for i in range(16):
        p(str(i+1),x0+i*cell,y,width=24,size=8,color=GOLD if i%4==0 else MUT)
    for r,(label,notes) in enumerate(rows):
        yy=y-23-r*rh;p(label,48,yy-3,width=76,size=9,color=MUT)
        for i in range(16):
            c.setFillColor(RED if i in notes else PANEL)
            c.roundRect(x0+i*cell,yy-19,21,21,2,fill=1,stroke=0)
    return y-30-len(rows)*rh

import json
pages=json.loads((DOC/'guide_en.json').read_text(encoding='utf-8'))
for data in pages:
    y=start(data['kicker'],data['title'],data.get('subtitle',''))
    for item in data['elements']:
        kind=item['type']
        if kind=='block':
            y=block(item['title'],item['text'],y)
        elif kind=='formula':
            y=formula(item['text'],item['caption'],y)
        elif kind=='grid':
            y=grid(y,[(name,set(notes)) for name,notes in item['rows']])-8
        elif kind=='hero':
            y=p(item['text'],48,y,size=33,bold=True,leading=39)-24
        elif kind=='intro':
            y=p(item['text'],48,y,size=14,leading=21)-25
        elif kind=='note':
            y=p(item['text'],48,y,size=9,color=MUT)-20
        else:
            y=p(item['text'],48,y,size=11)-24
c.save()
print(pdf)
print(f'{page} pages')
