exec(open('skin.py').read().split("import matplotlib")[0])
a,b=frame(200)
LEFT={51,63,66,126}|set(range(72,115,3))
out=[]; 
def add(name,mat,Q,faces=None): out.append((name,mat,Q))
# palma (centro de 51 y 126)
palm=None
groups=[]
for (ic,si,k),vs in objs.items():
    if not len(vs): continue
    P=np.array(vs)
    if ic=='16734' and k in LEFT: M=b['b'][k]; groups.append(('mano_izq_hueso_%d'%k,'mano',(M[:,:3]@P.T).T+M[:,3]))
    elif ic=='16251' and k==15: M=a['b'][k]; groups.append(('cargador','cargador',(M[:,:3]@P.T).T+M[:,3]))
    elif ic=='16251' and k in a['b']: M=a['b'][k]; groups.append(('arma_no_tocar_%d'%k,'arma',(M[:,:3]@P.T).T+M[:,3]))
pc=np.vstack([Q for n,m,Q in groups if n in('mano_izq_hueso_51','mano_izq_hueso_126')]).mean(0)
# cargador colocado bajo la palma (para que solo haya que afinar)
gi=[i for i,g in enumerate(groups) if g[0]=='cargador'][0]
n,m,Q=groups[gi]; right_end=Q[Q[:,0].argmax()]; d=pc+np.array([0,-0.03,0])-np.array([Q[:,0].max()-0.03,Q[:,1].mean(),Q[:,2].mean()])
groups[gi]=(n,m,Q+d)
np.save('mag_offset_inicial.npy',d)
with open('metro_mano_cargador.mtl','w') as f:
    for nm,c in [('mano','0.9 0.55 0.2'),('cargador','0.85 0.1 0.1'),('arma','0.6 0.6 0.6')]: f.write('newmtl %s\nKd %s\n\n'%(nm,c))
# caras: reconstruir con las del obj original
faces={}; cur=None; Vall=[]
for ln in open('/mnt/user-data/uploads/Temp/metro_vr_arma_16251_862638.obj'):
    if ln.startswith('v '): Vall.append(1)
    elif ln.startswith('o '):
        mm=re.match(r'pieza_(\d+)_(\d+)(_brazo)?(?:_hueso_(\d+))?',ln[2:].strip()); cur=(mm.group(1),mm.group(2),int(mm.group(4)) if mm.group(4) else -1); faces.setdefault(cur,[])
    elif ln.startswith('f ') and cur: faces[cur].append([int(t.split('/')[0])-1 for t in ln.split()[1:]])
keymap={}
for (ic,si,k) in objs:
    if ic=='16734' and k in LEFT: keymap['mano_izq_hueso_%d'%k]=(ic,si,k)
    elif ic=='16251' and k==15: keymap['cargador']=(ic,si,k)
    elif ic=='16251': keymap['arma_no_tocar_%d'%k]=(ic,si,k)
with open('metro_mano_cargador.obj','w') as f:
    f.write('# Metro VR: mano izquierda + cargador para posar en Blender\nmtllib metro_mano_cargador.mtl\n')
    base=1
    for n,m,Q in groups:
        key=keymap[n]; idx=sorted(set(i for fc in faces[key] for i in fc)); remap={g:j for j,g in enumerate(idx)}
        f.write('o %s\nusemtl %s\n'%(n,m))
        for v in Q: f.write('v %.5f %.5f %.5f\n'%tuple(v))
        for fc in faces[key]: f.write('f %s\n'%' '.join(str(remap[i]+base) for i in fc))
        base+=len(Q)
print(len(groups),'objetos', 'palma',np.round(pc,3))
