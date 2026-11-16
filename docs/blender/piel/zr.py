import numpy as np
from PIL import Image
def look(eye, target, up=(0,1,0)):
    f=np.array(target,float)-eye; f/=np.linalg.norm(f); u=np.array(up,float)
    r=np.cross(f,u); r/=np.linalg.norm(r); u=np.cross(r,f)
    return np.array([r,u,-f]), np.array(eye,float)
def render(objs, eye, target, fn, W=640, H=480, fov=40, up=(0,1,0)):
    R,e=look(eye,target,up); fl=0.5*H/np.tan(np.radians(fov/2))
    zb=np.full((H,W),np.inf); img=np.full((H,W,3),40,np.uint8)
    L=np.array([0.3,0.8,0.5]); L/=np.linalg.norm(L)
    for V,T,col in objs:
        if len(T)==0: continue
        P=(V-e)@R.T; T=np.asarray(T)
        a,b,c=P[T[:,0]],P[T[:,1]],P[T[:,2]]
        n=np.cross(b-a,c-a); nl=np.linalg.norm(n,axis=1)+1e-12; n/=nl[:,None]
        z=-(a[:,2]+b[:,2]+c[:,2])/3
        ok=(a[:,2]<-0.01)&(b[:,2]<-0.01)&(c[:,2]<-0.01)
        shade=0.35+0.65*np.abs(n@ (R@L))
        def proj(p): return np.stack([W/2+fl*p[:,0]/-p[:,2], H/2-fl*p[:,1]/-p[:,2]],1)
        A,Bp,C=proj(a),proj(b),proj(c)
        for i in np.where(ok)[0]:
            xs=[A[i,0],Bp[i,0],C[i,0]]; ys=[A[i,1],Bp[i,1],C[i,1]]
            x0,x1=int(max(0,np.floor(min(xs)))),int(min(W-1,np.ceil(max(xs)))); y0,y1=int(max(0,np.floor(min(ys)))),int(min(H-1,np.ceil(max(ys))))
            if x1<x0 or y1<y0: continue
            gx,gy=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            d=(Bp[i,1]-C[i,1])*(A[i,0]-C[i,0])+(C[i,0]-Bp[i,0])*(A[i,1]-C[i,1])
            if abs(d)<1e-9: continue
            w0=((Bp[i,1]-C[i,1])*(gx-C[i,0])+(C[i,0]-Bp[i,0])*(gy-C[i,1]))/d
            w1=((C[i,1]-A[i,1])*(gx-C[i,0])+(A[i,0]-C[i,0])*(gy-C[i,1]))/d
            w2=1-w0-w1; m=(w0>=0)&(w1>=0)&(w2>=0)
            if not m.any(): continue
            zz=z[i]; sub=zb[y0:y1+1,x0:x1+1]; m&=zz<sub
            sub[m]=zz; img[y0:y1+1,x0:x1+1][m]=(np.array(col)*shade[i]).clip(0,255).astype(np.uint8)
    Image.fromarray(img).save(fn)
