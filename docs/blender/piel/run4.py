from solve3 import *
W_=ph(57,S); MI,MM,MP=ph(12,S),ph(21,S),ph(39,S)
f=(MI+MM+MP)/3-W_; f/=np.linalg.norm(f)
a=MI-MP; a-=f*(a@f); a/=np.linalg.norm(a)
n=np.cross(f,a)
tip=Pidle[dom==27].mean(0)
if (tip-MM)@n<0: n=-n
ang=float(os.environ.get('ANG','0'))   # tilt of the magazine in the palm plane (deg)
ca,sa=np.cos(np.radians(ang)),np.sin(np.radians(ang))
L=ca*a+sa*f; Dp=-sa*a+ca*f
tx,ty=L,Dp; tz=np.cross(tx,ty)
if tz@n<0: ty=-ty; tz=-tz
R=np.column_stack([tx,ty,tz])@np.vstack(ax)   # maps magazine axes -> target
mid=(MI+MM+MP)/3
off_f=float(os.environ.get('OF','-0.02')); off_a=float(os.environ.get('OA','0.0455'))
c=mid+f*off_f+a*off_a
D=place(c,R)
pal=Pidle[np.isin(dom,[57])]
# slide along n until the palm just touches (from inside palm outwards)
pass
for it in range(200):
    if box_sd(D,pal).min()>=0.0015: break
    D[:3,3]+=n*0.001
print('palm sd',box_sd(D,pal).min().round(4),'moved',it)
th=solve3(D)
Sn=compose(th)
json.dump({'S':{str(k):Sn[k][:3].tolist() for k in LEFT},'D_mag':D.tolist(),'th':{str(k):v for k,v in th.items()}},open('/mnt/user-data/outputs/metro_mano_pose.json','w'))
print('n world',np.array([n[0],-n[2],n[1]]).round(3),'a world',np.array([a[0],-a[2],a[1]]).round(3),'f world',np.array([f[0],-f[2],f[1]]).round(3))
Pn=skinv(Sn)
ctr=D[:3,:3]@cen+D[:3,3]
for nm,bs in (('palma57',[57]),('prox',[12,21,30,39]),('medio',[15,24,33,42]),('punta',[18,27,36,45]),('pulgar',[3,6,9])):
    q=Pn[np.isin(dom,bs)]-ctr
    print(nm,'along n: %.3f'%(q@n).mean(),'along f: %.3f'%(q@f).mean(),'along a: %.3f'%(q@a).mean())
print('mag ctr - mid along n', ((ctr-mid)@n).round(3))
