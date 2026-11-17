import numpy as np
from pose import h4, S0, BN
from load import load, M
arm=load('/mnt/user-data/uploads/Temp/metro_vr_piel_16251_862638.txt')[(16734,177912)]
S={k:h4(v) for k,v in S0.items()}
CH={3:57,6:3,9:6,12:57,15:12,18:15,21:57,24:21,27:24,30:57,33:30,36:33,39:57,42:39,45:42}
def q(R):
    w=np.sqrt(max(1e-12,1+np.trace(R)))/2; return np.array([w,(R[2,1]-R[1,2])/(4*w),(R[0,2]-R[2,0])/(4*w),(R[1,0]-R[0,1])/(4*w)])
def Rq(a):
    w,x,y,z=a/np.linalg.norm(a)
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def blend(Sopen, off, a):
    Sn=dict(S); hb=57 if off==0 else 126
    for k,p in CH.items():
        kk=k+off; pp=(p+off if p!=57 else hb)
        Jo=np.linalg.inv(Sopen[pp])@Sopen[kk]; Ji=np.linalg.inv(S[pp])@S[kk]
        from scipy.spatial.transform import Rotation as Ro, Slerp
        sl=Slerp([0,1],Ro.from_matrix([Jo[:3,:3],Ji[:3,:3]]))
        J=np.eye(4); J[:3,:3]=sl([a]).as_matrix()[0]; pv=np.array(BN[kk]['head']); J[:3,3]=pv-J[:3,:3]@pv
        Sn[kk]=Sn[pp]@J
    return Sn
L=np.load('relaxL.npy',allow_pickle=True).item(); R=np.load('relaxR.npy',allow_pickle=True).item()
SnL=blend(L,0,0.25); SnR=blend(R,69,0.25)
np.save('relaxL2.npy',SnL); np.save('relaxR2.npy',SnR)
f=lambda m: ', '.join('%.6ff'%x for x in np.asarray(m)[:3].reshape(-1))
row=lambda Sn,k: Sn[k] if k in Sn else h4(M(arm,k))
out='/* (E67) manos RELAJADAS (sueltas): dedos de la postura mas abierta de la animacion del juego (grabacion 7: izq t=688, der t=2547) con un 25 % de la de agarre, sobre los brazos del export (E60). */\n'
for nm,Sn in (('L',SnL),('R',SnR)):
    out+='static const float RELAX_%s[GRIP_NB][12] = {\n'%nm+',\n'.join('    { %s }'%f(row(Sn,k)) for k in range(0,138,3))+'\n};\n'
open('relax.h','w').write(out)
