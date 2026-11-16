import numpy as np, json, sys
from pose import *
E=json.load(open('escena.json'))
S={k:h4(v) for k,v in S0.items()}
Pidle=skinv(S)
# hinge axes from recordings
sys.argv=['x','/mnt/user-data/uploads/Temp/metro_vr_grabacion.txt']
exec(open('../g2.py').read().split("print(len(recs))")[0])
frames=[F for r in recs for F in [s['b'] for s in r if s['k']=='brazos' and len(s['b'])>=40]]
def logR(R):
    c=np.clip((np.trace(R)-1)/2,-1,1); th=np.arccos(c)
    if th<1e-6: return np.zeros(3)
    w=np.array([R[2,1]-R[1,2],R[0,2]-R[2,0],R[1,0]-R[0,1]])/(2*np.sin(th)); return w*th
HINGE={}
for k,b in BN.items():
    p=b['parent']
    if p<0: continue
    rv=np.array([logR(F[p][:,:3].T@F[k][:,:3]) for F in frames if p in F and k in F])
    rv=rv-rv.mean(0)
    u=np.linalg.svd(rv,full_matrices=False)[2][0]
    HINGE[k]=u
mb=np.array(E['mag_bind']); M15=h4(np.array(E['M15'])); mp=mb@M15[:3,:3].T+M15[:3,3]
c0=mp.mean(0); ax=np.linalg.svd(mp-c0,full_matrices=False)[2]
if ax[0]@M15[:3,0]<0: ax[0]=-ax[0]
loc=(mp-c0)@ax.T; lo,hi=loc.min(0),loc.max(0); cen=c0+ax.T@((lo+hi)/2); half=(hi-lo)/2
def box_sd(D, pts):
    Di=np.linalg.inv(D); q=pts@Di[:3,:3].T+Di[:3,3]
    d=np.abs((q-cen)@ax.T)-half
    return np.linalg.norm(np.maximum(d,0),axis=1)+np.minimum(d.max(1),0)
def place(center, R):
    D=np.eye(4); D[:3,:3]=R; D[:3,3]=center-R@cen; return D
def Ry(t):
    c,s=np.cos(t),np.sin(t); return np.array([[c,0,s],[0,1,0],[-s,0,c]])
CH={3:57,6:3,9:6,12:57,15:12,18:15,21:57,24:21,27:24,30:57,33:30,36:33,39:57,42:39,45:42}
def compose(th):
    Sn=dict(S)
    for chain in FING:
        for k in chain:
            p=CH[k]; piv=np.array(BN[k]['head'])
            R=rot(HINGE[k],th.get(k,0.0),piv)
            Sn[k]=Sn[p]@R@np.linalg.inv(S[p])@S[k]
    return Sn
def verts_after(Sn,bones):
    sel=np.isin(dom,bones); idx=np.where(sel)[0]; P=np.zeros((len(idx),3))
    Vi=V[idx]
    for j in range(4):
        bj=B[idx,j]; wj=W[idx,j]
        for kb in np.unique(bj[wj>0]):
            m=Sn.get(int(kb),S.get(int(kb)))
            if m is None: continue
            s_=(bj==kb)&(wj>0)
            P[s_]+=wj[s_,None]*(Vi[s_]@m[:3,:3].T+m[:3,3])
    return P
def solve(D, margin=0.0015, maxdeg=60, verbose=True):
    th={}
    ctr=D[:3,:3]@cen+D[:3,3]
    for chain in FING:
        for i,k in enumerate(chain):
            dist=list(chain[i:])
            def pts(t):
                th2=dict(th); th2[k]=t; return verts_after(compose(th2),dist)
            # closing sign: reduces distance of chain verts to magazine center
            tipb=[chain[-1]]
            def tip(t):
                th2=dict(th); th2[k]=t; return verts_after(compose(th2),tipb).mean(0)
            sg=1 if np.linalg.norm(tip(0.1)-ctr)<np.linalg.norm(tip(-0.1)-ctr) else -1
            free=lambda t: box_sd(D,pts(sg*t)).min()>=margin
            t_sel=0.0
            if free(0):
                for t in np.radians(np.arange(2,maxdeg+1,2)):
                    if free(t): t_sel=t
                    else: break
            else:
                for t in np.radians(np.arange(-2,-61,-2)):
                    if free(t): t_sel=t; break
                else: t_sel=np.radians(-60)
            th[k]=sg*t_sel
            if verbose: print(k,'sign',sg,'deg',round(np.degrees(t_sel),1))
    return th
