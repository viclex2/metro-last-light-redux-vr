from grasp2 import *
import os, json
for r,m in ((12,15),(21,24),(30,33),(39,42)): HINGE[r]=HINGE[m]
ref=HINGE[24]
for k in (12,15,18,21,24,27,30,33,36,39,42,45):
    if HINGE[k]@ref<0: HINGE[k]=-HINGE[k]
# global flex sign: flexing middle PIP reduces tip->wrist distance
wr=ph(57,S)
def tipd(th): return np.linalg.norm(verts_after(compose(th),[27]).mean(0)-wr)
FS=1 if tipd({24:0.2})<tipd({24:-0.2}) else -1
print('flex sign',FS)
# thumb: sign per joint toward magazine center
def solve3(D, margin=0.0015, open_deg=40, close_deg=70, verbose=True):
    th={}; ctr=D[:3,:3]@cen+D[:3,3]
    for chain in FING:
        for i,k in enumerate(chain):
            dist=list(chain[i:])
            def pts(t):
                th2=dict(th); th2[k]=t; return verts_after(compose(th2),dist)
            if chain[0]==3:
                tp=lambda t: np.linalg.norm(verts_after(compose({**th,k:t}),[9]).mean(0)-ctr)
                sg=1 if tp(0.15)<tp(-0.15) else -1
            else: sg=FS
            free=lambda t: box_sd(D,pts(sg*t)).min()>=margin
            t_sel=np.radians(-open_deg)
            for t in np.radians(np.arange(-open_deg,close_deg+1,2)):
                if free(t): t_sel=t
                else: break
            th[k]=sg*t_sel
            if verbose: print(k,'deg',round(np.degrees(t_sel),1))
    return th
