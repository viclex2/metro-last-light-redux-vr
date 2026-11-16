import numpy as np, json
E=json.load(open('escena.json'))
A=E["arm"]; V=np.array(A['v']); T=np.array(A['t']); B=np.array(A['b']); W=np.array(A['w'])
BN={b['k']:b for b in E['bones']}
S0={k:np.array(b['S']) for k,b in BN.items()}
def h4(m): r=np.eye(4); r[:3]=m; return r
def rot(axis,ang,pivot):
    axis=np.asarray(axis,float); axis/=np.linalg.norm(axis); x,y,z=axis; c,s=np.cos(ang),np.sin(ang); C=1-c
    R=np.array([[c+x*x*C,x*y*C-z*s,x*z*C+y*s],[y*x*C+z*s,c+y*y*C,y*z*C-x*s],[z*x*C-y*s,z*y*C+x*s,c+z*z*C]])
    m=np.eye(4); m[:3,:3]=R; m[:3,3]=pivot-R@pivot; return m
def skinv(S,mask=None):
    out=np.zeros_like(V)
    for j in range(4):
        for k in np.unique(B[:,j]):
            sel=(B[:,j]==k)&(W[:,j]>0)
            if not sel.any(): continue
            m=S.get(int(k), S0.get(int(k)))
            if m is None: continue
            m=np.asarray(m)[:3]
            out[sel]+=W[sel,j,None]*(V[sel]@m[:,:3].T+m[:,3])
    return out
def ph(k,S): # posed head of bone k
    m=np.asarray(S[k])[:3]; return m[:,:3]@np.array(BN[k]['head'])+m[:,3]
def pt(k,S):
    m=np.asarray(S[k])[:3]; return m[:,:3]@np.array(BN[k]['tail'])+m[:,3]
LEFT=[0,63,66,48,51,54,57,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45]
FING=[(3,6,9),(12,15,18),(21,24,27),(30,33,36),(39,42,45)]
dom=B[np.arange(len(V)),W.argmax(1)]
