import numpy as np
from pose import *
import pose
from zr import render
from PIL import Image
SnL=np.load('relaxL2.npy',allow_pickle=True).item(); SnR=np.load('relaxR2.npy',allow_pickle=True).item()
RIGHT=[69,132,135,117,120,123,126,72,75,78,81,84,87,90,93,96,99,102,105,108,111,114]
def hand(Sn,bones,hb,fn,col=(225,170,130)):
    P=skinv(Sn); TT=pose.T
    sel=np.isin(dom,[b for b in bones if b not in (0,63,66,48,69,132,135,117)])
    T=TT[sel[TT].all(1)]
    c=P[dom==hb].mean(0)
    ims=[]
    for i,e in enumerate(((0.25,0.05,0.05),(-0.25,0.05,0.05),(0.0,0.3,0.05),(0.05,-0.05,0.3))):
        render([(P,T,col)],c+np.array(e),c,'_r%d.png'%i,W=420,H=360); ims.append(Image.open('_r%d.png'%i))
    o=Image.new('RGB',(420*4,360))
    for i,im in enumerate(ims): o.paste(im,(420*i,0))
    o.save(fn)
S={k:h4(v) for k,v in S0.items()}
hand(S,LEFT,57,'idleL.png'); hand(SnL,LEFT,57,'relL.png'); hand(SnR,RIGHT,126,'relR.png')
Image.open('idleL.png'); 
a,b,c=[Image.open(f) for f in ('idleL.png','relL.png','relR.png')]
o=Image.new('RGB',(1680,1080)); o.paste(a,(0,0)); o.paste(b,(0,360)); o.paste(c,(0,720)); o.save('rel_all.png')
