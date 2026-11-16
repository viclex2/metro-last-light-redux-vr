import numpy as np
def load(fn):
    P={}; cur=None
    for ln in open(fn):
        a=ln.split()
        if not a or a[0]=='#': continue
        if a[0]=='pieza':
            cur=dict(ic=int(a[1]),si=int(a[2]),arm=int(a[3]),v=[],b=[],w=[],t=[],pal=[]); P[(cur['ic'],cur['si'])]=cur
        elif a[0]=='xf': cur['xf']=np.array([float(x) for x in a[1:]]).reshape(4,4)
        elif a[0]=='r': cur['pal'].append([float(x) for x in a[2:6]])
        elif a[0]=='v': cur['v'].append([float(x) for x in a[1:4]]); cur['b'].append([int(x) for x in a[4:8]]); cur['w'].append([float(x) for x in a[8:12]])
        elif a[0]=='t': cur['t'].append([int(x) for x in a[1:4]])
    for p in P.values():
        for k in ('v','b','w','t','pal'): p[k]=np.array(p[k])
    return P
def M(p,b):  # skin matrix 3x4 for BONES value b
    return p['pal'][b+1:b+4]
def skin(p):
    out=np.zeros_like(p['v'])
    for j in range(4):
        for i in range(len(p['v'])):
            w=p['w'][i,j]
            if w<=0: continue
            m=M(p,p['b'][i,j]); out[i]+=w*(m[:,:3]@p['v'][i]+m[:,3])
    return out
