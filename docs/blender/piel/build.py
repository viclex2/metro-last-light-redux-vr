import numpy as np, json, sys
from load import *
P=load('/mnt/user-data/uploads/Temp/metro_vr_piel_16251_862638.txt')
a=P[(16734,177912)]; g=P[(16251,862638)]
par={63:0,66:63,48:66,51:66,54:51,57:54, 3:57,6:3,9:6, 12:57,15:12,18:15, 21:57,24:21,27:24, 30:57,33:30,36:33, 39:57,42:39,45:42,
     132:69,135:132,117:135,120:135,123:120,126:123, 72:126,75:72,78:75, 81:126,84:81,87:84, 90:126,93:90,96:93, 99:126,102:99,105:102, 108:126,111:108,114:111}
roots=[0,69]
V=a['v']; B=a['b']; W=a['w']
def wmask(b,th=0.05): return ((B==b)&(W>th)).any(1)
head={}
for b in roots: head[b]=V[wmask(b)].mean(0)
for c,p in par.items():
    m=wmask(c)&wmask(p)
    head[c]=V[m].mean(0) if m.sum()>=3 else V[wmask(c)].mean(0)
kids={}
for c,p in par.items(): kids.setdefault(p,[]).append(c)
main_child={0:63,63:66,66:51,51:54,54:57,57:21, 3:6,6:9,12:15,15:18,21:24,24:27,30:33,33:36,39:42,42:45,
            69:132,132:135,135:120,120:123,123:126,126:90, 72:75,75:78,81:84,84:87,90:93,93:96,99:102,102:105,108:111,111:114}
tail={}
allb=roots+list(par)
for b in allb:
    if b in main_child: tail[b]=head[main_child[b]]
    else:
        p=par[b]; d=head[b]-head[p]; d/=np.linalg.norm(d)
        dv=V[wmask(b,0.3)]; L=max(((dv-head[b])@d).max(),0.01) if len(dv) else 0.02
        tail[b]=head[b]+d*L
    if np.linalg.norm(tail[b]-head[b])<0.005:
        tail[b]=head[b]+np.array([0,0.01,0])
bones=[]
for b in allb:
    bones.append(dict(k=int(b),parent=int(par[b]) if b in par else -1,head=head[b].tolist(),tail=tail[b].tolist(),S=M(a,b).tolist()))
# gun posed, magazine split
G=skin(g); gd=g['b'][np.arange(len(g['v'])),g['w'].argmax(1)]
t=g['t']; mag=(gd[t]==15).all(1)
out=dict(arm=dict(v=V.tolist(),t=a['t'].tolist(),b=B.tolist(),w=W.tolist()),bones=bones,
  gun=dict(v=G.tolist(),t=t[~mag].tolist()),mag=dict(v=G.tolist(),t=t[mag].tolist()),M15=M(g,15).tolist(),M21=M(g,21).tolist())
bl=P[(4320,857940)]; out['bul']=dict(v=skin(bl).tolist(),t=bl['t'].tolist())
json.dump(out,open('escena.json','w'))
print(len(bones),'mag tris',mag.sum(),'gun tris',(~mag).sum())
for b in (57,3,12,21,30,39): print(b,np.round(head[b],3),np.round(tail[b],3))
