import numpy as np, json, sys
sys.argv=['x','/mnt/user-data/uploads/Temp/metro_vr_grabacion.txt']
src=open('../g2.py').read().split("print(len(recs))")[0]
exec(src)
E=json.load(open('escena.json'))
BN={b['k']:b for b in E['bones']}
frames=[{k:np.array(b['S']) for k,b in BN.items()}]
for r in recs:
    for s in r:
        if s['k']=='brazos' and len(s['b'])>=40: frames.append(s['b'])
print('frames',len(frames))
lam=0.03
for k,b in BN.items():
    p=b['parent']
    if p<0: continue
    A=[];y=[]
    for F in frames:
        if k not in F or p not in F: continue
        Sp,Sc=F[p],F[k]
        A.append(Sp[:,:3]-Sc[:,:3]); y.append(Sc[:,3]-Sp[:,3])
    A=np.vstack(A); y=np.concatenate(y); c0=np.array(b['head'])
    A2=np.vstack([A,lam*np.eye(3)]); y2=np.concatenate([y,lam*c0])
    sol=np.linalg.lstsq(A2,y2,rcond=None)[0]
    res=np.linalg.norm(A@sol-y)/np.sqrt(len(y)/3)
    sv=np.linalg.svd(A,compute_uv=False)
    print(k,'<-',p,'old',np.round(c0,3),'new',np.round(sol,3),'res %.4f'%res,'sv',np.round(sv,2))
    b['head']=sol.tolist()
json.dump(E,open('escena.json','w'))
