import os
os.environ.setdefault('X','')
from grasp2 import *
CHR={k+69:(v+69 if v!=57 else 126) for k,v in CH.items()}
CHA={**CH,**CHR}
FINGR=[tuple(k+69 for k in f) for f in FING]
for r,m in ((12,15),(21,24),(30,33),(39,42)):
    HINGE[r]=HINGE[m]; HINGE[r+69]=HINGE[m+69]
def compose2(th, fings):
    Sn=dict(S)
    for chain in fings:
        for k in chain:
            p=CHA[k]; piv=np.array(BN[k]['head'])
            Sn[k]=Sn[p]@rot(HINGE[k],th.get(k,0.0),piv)@np.linalg.inv(S[p])@S[k]
    return Sn
NEXT={}
for chain in FING+FINGR:
    for a,b in zip(chain,chain[1:]): NEXT[a]=b
def joint(Sn,k):  # posed joint position = head of k transformed by its own S
    m=Sn[k]; return m[:3,:3]@np.array(BN[k]['head'])+m[:3,3]
def seg_end(Sn,k):
    if k in NEXT: return joint(Sn,NEXT[k])
    m=Sn[k]; return m[:3,:3]@np.array(BN[k]['tail'])+m[:3,3]
def bend(Sn,k):
    p=CHA[k]
    a=joint(Sn,k)-(joint(Sn,p) if p not in (57,126) else joint(Sn,p))
    if p in (57,126):  # hand direction: wrist -> this knuckle
        a=joint(Sn,k)-joint(Sn,p)
    b=seg_end(Sn,k)-joint(Sn,k)
    a/=np.linalg.norm(a); b/=np.linalg.norm(b)
    return np.degrees(np.arccos(np.clip(a@b,-1,1)))
def relax(fings, targets):
    th={}
    for chain,tg in zip(fings,targets):
        for k,t in zip(chain,tg):
            best=None
            for d in np.radians(np.arange(-120,121,1.0)):
                th2=dict(th); th2[k]=d; b=bend(compose2(th2,fings),k)
                if best is None or abs(b-t)<best[0]: best=(abs(b-t),d)
            th[k]=best[1]
    return th
# targets per chain (thumb, index, middle, ring, pinky): (base, mid, tip) bend in degrees
TG=[(25,15,10),(12,15,8),(15,20,10),(20,25,12),(25,30,15)]
if __name__=='__main__':
    S0d={k:h4(v) for k,v in S0.items()}
    for nm,fings in (('L',FING),('R',FINGR)):
        print(nm,'idle bends',[[round(bend(S,k)) for k in c] for c in fings])
    thL=relax(FING,TG); thR=relax(FINGR,TG)
    SnL=compose2(thL,FING); SnR=compose2(thR,FINGR)
    for nm,Sn,fings in (('L',SnL,FING),('R',SnR,FINGR)):
        print(nm,'relaxed bends',[[round(bend(Sn,k)) for k in c] for c in fings])
    np.save('relaxL.npy',{k:v for k,v in SnL.items()}); np.save('relaxR.npy',{k:v for k,v in SnR.items()})
