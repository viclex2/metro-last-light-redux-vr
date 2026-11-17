import numpy as np, sys, json
sys.argv=['x','/mnt/user-data/uploads/Temp/metro_vr_grabacion.txt']
exec(open('../g2.py').read().split("print(len(recs))")[0])
from pose import BN
CH={3:57,6:3,9:6,12:57,15:12,18:15,21:57,24:21,27:24,30:57,33:30,36:33,39:57,42:39,45:42}
def ang(F,k,p): 
    R=F[p][:,:3].T@F[k][:,:3]; return np.degrees(np.arccos(np.clip((np.trace(R)-1)/2,-1,1)))
res=[]
for ri,r in enumerate(recs):
    for s in r:
        if s['k']!='brazos' or len(s['b'])<40: continue
        F=s['b']
        for side,off in (('L',0),('R',69)):
            tot=sum(ang(F,k+off,(p+off if p!=57 else (57 if off==0 else 126))) for k,p in CH.items() if k not in (3,6,9))
            res.append((tot,side,ri,s['t']))
res.sort()
for side in 'LR':
    xs=[x for x in res if x[1]==side]; print(side,'most open',xs[:3],'most closed',xs[-1])
from pose import h4, S0
S={k:h4(v) for k,v in S0.items()}
def apply(F, off):
    Sn=dict(S); hb=57 if off==0 else 126
    for k,p in CH.items():   # CH ordered parent-first
        kk=k+off; pp=(p+off if p!=57 else hb)
        J=np.linalg.inv(h4(F[pp]))@h4(F[kk]); Sn[kk]=Sn[pp]@J
    return Sn
def frame(ri,t): return min([s for s in recs[ri] if s['k']=='brazos'],key=lambda s:abs(s['t']-t))['b']
SnL=apply(frame(7,688),0); SnR=apply(frame(7,2547),69)
np.save('relaxL.npy',SnL); np.save('relaxR.npy',SnR)
