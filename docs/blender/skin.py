import sys, numpy as np, re
exec(open('g2.py').read().split("print(len(recs))")[0].replace("sys.argv[1]","'/mnt/user-data/uploads/Temp/metro_vr_grabacion.txt'"))
objs={}; cur=None; V=[]
for ln in open('/mnt/user-data/uploads/Temp/metro_vr_arma_16251_862638.obj'):
    if ln.startswith('v '): V.append([float(x) for x in ln.split()[1:4]])
    elif ln.startswith('o '):
        m=re.match(r'pieza_(\d+)_(\d+)(_brazo)?(?:_hueso_(\d+))?',ln[2:].strip()); cur=(m.group(1),m.group(2),int(m.group(4)) if m.group(4) else -1); objs.setdefault(cur,set())
    elif ln.startswith('f ') and cur:
        for t in ln.split()[1:]: objs[cur].add(int(t.split('/')[0])-1)
objs={k:[V[i] for i in sorted(v)] for k,v in objs.items()}
r=recs[8]
def frame(T):
    a=min([s for s in r if s['k']=='arma' and s['t']<100000],key=lambda s:abs(s['t']-T))
    b=min([s for s in r if s['k']=='brazos' and s['t']<100000],key=lambda s:abs(s['t']-T))
    return a,b
def skin(T, raw=False):
    a,b=frame(T); out=[]
    for (ic,si,k),vs in objs.items():
        P=np.array(vs)
        if len(P)==0: continue
        arm = ic=='16734' or ic=='6' 
        pal = b['b'] if arm else a['b']
        if raw or k<0 or k not in pal: Q=P
        else:
            M=pal[k]; Q=(M[:,:3]@P.T).T+M[:,3]
        kind='arm' if ic=='16734' else ('mag' if (ic=='16251' and k==15) or ic in('4320','180') else 'gun')
        out.append((kind,k,Q))
    return out
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def draw(T,fn,raw=False):
    o=skin(T,raw); fig,ax=plt.subplots(1,3,figsize=(18,6))
    col={'arm':'tab:orange','mag':'tab:red','gun':'0.6'}
    for kind,k,Q in o:
        if len(Q)==0: continue
        for i,(x,y) in enumerate([(0,1),(0,2),(2,1)]):
            ax[i].scatter(Q[:,x],Q[:,y],s=0.3,c=col[kind])
    for i,t in enumerate(['x-y frente','x-z arriba','z-y lado']): ax[i].set_title('%s t=%d'%(t,T)); ax[i].set_aspect('equal')
    plt.savefig(fn,dpi=70); plt.close()
draw(0,'s_raw.png',True); draw(200,'s_idle.png'); draw(1390,'s_grip.png')
