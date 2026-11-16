import sys, numpy as np
recs=[];cur=None;samp=None
for ln in open(sys.argv[1]):
    if ln.startswith('#'): cur=[];recs.append(cur);continue
    if ln.startswith('T '):
        p=ln.split(); samp={'t':int(p[1]) if p[1].isdigit() else -1,'k':p[2],'b':{}}; cur.append(samp); continue
    if ln.startswith('h') and samp is not None:
        p=ln.split()
        if len(p)==13: samp['b'][int(p[0][1:])]=np.array([float(x) for x in p[1:]]).reshape(3,4)
print(len(recs))
for i,r in enumerate(recs):
    a=[s for s in r if s['k']=='arma']; b=[s for s in r if s['k']=='brazos']
    kb=sorted(b[0]['b'].keys()) if b else []
    print(i,len(a),len(b),'brazos huesos',len(kb),kb[:3],kb[-3:])
r=recs[8]
a=[s for s in r if s['k']=='arma']; b=[s for s in r if s['k']=='brazos']
print('t range',a[0]['t'],a[-1]['t'])
for idx in range(0,len(a),25):
    sa=a[idx]; sb=min(b,key=lambda s:abs(s['t']-sa['t']))
    m15=sa['b'][15][:,3]; f21=sa['b'][21][:,3]
    d=sorted((np.linalg.norm(sb['b'][k][:,3]-m15),k) for k in sb['b'])
    print(sa['t'],'m15',np.round(m15,3),'f21',np.round(f21,3),'cercanos',[(k,round(x,3)) for x,k in d[:4]])
