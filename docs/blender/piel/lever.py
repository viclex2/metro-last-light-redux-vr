import numpy as np, json, sys
sys.argv=['x','/mnt/user-data/uploads/Temp/metro_vr_grabacion.txt']
exec(open('../g2.py').read().split("print(len(recs))")[0])
E=json.load(open('escena.json'))
A=E['arm']; V=np.array(A['v']); B=np.array(A['b']); W=np.array(A['w'])
dom=B[np.arange(len(V)),W.argmax(1)]
from load import load
P=load('/mnt/user-data/uploads/Temp/metro_vr_piel_16251_862638.txt'); g=P[(16251,862638)]
gv=g['v']*E['c']; gd=g['b'][np.arange(len(gv)),g['w'].argmax(1)]
lv=gv[gd==33]
for ri,r in enumerate(recs):
  arma=[s for s in r if s['k']=='arma']; braz=[s for s in r if s['k']=='brazos']
  if not arma or not braz or 33 not in arma[0]['b']: continue
  # lever travel relative to 21
  def rel(s):
    M21=s['b'][21]; M33=s['b'][33]; return M21[:,:3].T@(M33[:,3]-M21[:,3])
  r0=rel(arma[0]); best=None
  for s in arma:
    d=np.linalg.norm(rel(s)-r0)
    if best is None or d>best[0]: best=(d,s['t'])
  if best[0]<0.02: continue
  T=best[1]; a=min(arma,key=lambda s:abs(s['t']-T)); b=min(braz,key=lambda s:abs(s['t']-T))
  M33=a['b'][33]; lc=(lv@M33[:,:3].T+M33[:,3]).mean(0)
  def hp(k): S=b['b'][k]; return (V[dom==k]@S[:,:3].T+S[:,3]).mean(0)
  print('rec',ri,'max lever travel %.3f at t=%d'%best,'dist R126 %.3f L57 %.3f R-fingers %.3f L-fingers %.3f'%(np.linalg.norm(hp(126)-lc),np.linalg.norm(hp(57)-lc),np.linalg.norm(hp(84)-lc),np.linalg.norm(hp(15)-lc)))
r=recs[8]; arma=[s for s in r if s['k']=='arma']; braz=[s for s in r if s['k']=='brazos']
def rel(s): M21=s['b'][21]; M33=s['b'][33]; return M21[:,:3].T@(M33[:,3]-M21[:,3])
r0=rel(arma[0])
for s in arma:
    if 2200<s['t']<3300 and s['t']%3==0 or False: pass
ts=[(s['t'],np.linalg.norm(rel(s)-r0)) for s in arma if 2000<s['t']<3400]
for t,d in ts[::6]:
    b=min(braz,key=lambda x:abs(x['t']-t)); a=min(arma,key=lambda x:abs(x['t']-t))
    M33=a['b'][33]; lc=(lv@M33[:,:3].T+M33[:,3]).mean(0)
    S=b['b'][84]; fp=(V[dom==84]@S[:,:3].T+S[:,3]).mean(0)
    print(t,'travel %.3f'%d,'R index-mid finger to lever %.3f'%np.linalg.norm(fp-lc))
