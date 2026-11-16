import numpy as np, json, sys, os
from pose import *
from zr import render
from PIL import Image
import pose
E=json.load(open('escena.json')); G=np.array(E['gun']['v'])
def scene(Sd, hide_left=False, right=False):
    S={k:h4(v) for k,v in S0.items()}; S.update(Sd)
    P=skinv(S); TT=pose.T
    objs=[(G,E['lever']['t'],(230,40,40)),(G,E['cover']['t'],(40,180,60))]
    if not hide_left: keep=np.isin(dom,LEFT); objs.append((P,TT[keep[TT].all(1)],(225,170,130)))
    if right:
        RIGHT=[69,132,135,117,120,123,126,72,75,78,81,84,87,90,93,96,99,102,105,108,111,114]
        keep=np.isin(dom,RIGHT); objs.append((P,TT[keep[TT].all(1)],(205,150,115)))
    objs+= [(G,E['gun']['t'],(110,110,115)),(G,E['mag']['t'],(60,80,170))]
    return objs
def shots(objs, fn, tg=np.array([0.15,-0.07,0.52]), eyes=((0.35,0.12,0.0),(0.2,0.3,0.05),(0.15,-0.05,0.3))):
    ims=[]
    for i,e in enumerate(eyes):
        render(objs, tg+np.array(e), tg, '_t%d.png'%i); ims.append(Image.open('_t%d.png'%i))
    o=Image.new('RGB',(640*len(ims),480))
    for i,im in enumerate(ims): o.paste(im,(640*i,0))
    o.save(fn)
