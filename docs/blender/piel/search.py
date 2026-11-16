import os
os.environ.setdefault('X','')
exec(open('run4.py').read().split("ang=float")[0])
import itertools, json
def build(swap,ang,of,oa=0.0455):
    ca,sa=np.cos(np.radians(ang)),np.sin(np.radians(ang))
    L=ca*a+sa*f; Dp=-sa*a+ca*f
    axs=[ax[0],ax[2],ax[1]] if swap else [ax[0],ax[1],ax[2]]
    tx,ty=L,Dp; tz=np.cross(tx,ty)
    R=np.column_stack([tx,ty,tz])@np.vstack(axs)
    if np.linalg.det(R)<0: R=np.column_stack([tx,-ty,-tz])@np.vstack(axs)  # keep proper rotation
    mid=(MI+MM+MP)/3; D=place(mid+f*of+a*oa,R)
    thw=((B==3)&(W>0.05)).any(1)|((B==6)&(W>0.05)).any(1)
    pal=Pidle[np.isin(dom,[57])&~thw]
    for it in range(120):
        if box_sd(D,pal).min()>=-0.002: break
        D[:3,3]+=n*0.001
    return D
def quality(D,th):
    Sn=compose(th); score=0
    for k in (12,15,18,21,24,27,30,33,36,39,42,45,3,6,9):
        sd=box_sd(D,verts_after(Sn,[k])).min()
        score+= (sd<0.004)
    opening=sum(max(0,-v*FS) for k,v in th.items() if k not in (3,6,9))
    return score-2*opening
best=None
for swap in (0,1):
    for ang in (0,10,20,30):
        for of in (-0.05,-0.04,-0.03,-0.02):
            D=build(swap,ang,of); th=solve3(D,verbose=False); mv=((D[:3,:3]@cen+D[:3,3]-(MI+MM+MP)/3)@n); q=quality(D,th)
            print(swap,ang,of,'q %.2f'%q,'n %.3f'%mv,{k:round(np.degrees(v*(FS if k not in (3,6,9) else 1))) for k,v in th.items()})
            if best is None or q>best[0]: best=(q,swap,ang,of,D,th)
q,swap,ang,of,D,th=best; print('BEST',q,swap,ang,of)
Sn=compose(th)
json.dump({'S':{str(k):Sn[k][:3].tolist() for k in LEFT},'D_mag':D.tolist(),'th':{str(k):v for k,v in th.items()}},open('/mnt/user-data/outputs/metro_mano_pose.json','w'))
