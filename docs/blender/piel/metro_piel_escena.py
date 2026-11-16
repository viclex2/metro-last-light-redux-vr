# Metro VR (E60): escena de Blender con la mano izquierda y su esqueleto de verdad + cargador
import bpy, json, os, mathutils
from mathutils import Matrix, Vector
TMP=os.environ.get('TEMP', r'C:\Users\vicle\AppData\Local\Temp')
E=json.load(open(os.path.join(TMP,'metro_piel_escena.json')))
LEFT=[0,63,66,48,51,54,57,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45]
# limpiar
for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
for m in list(bpy.data.meshes): bpy.data.meshes.remove(m)
for a in list(bpy.data.armatures): bpy.data.armatures.remove(a)
col=bpy.context.scene.collection
root=bpy.data.objects.new('Metro', None); col.objects.link(root)
root.rotation_euler=(1.5707963, 0, 0)
def mat(name, rgba):
    m=bpy.data.materials.get(name) or bpy.data.materials.new(name); m.diffuse_color=rgba; return m
def mkmesh(name, V, T, color, parent):
    me=bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in V], [], [tuple(t) for t in T]); me.update()
    me.materials.append(mat(name+'_mat', color))
    ob=bpy.data.objects.new(name, me); col.objects.link(ob); ob.parent=parent; ob.matrix_parent_inverse=Matrix.Identity(4)
    for p in me.polygons: p.use_smooth=True
    return ob
# --- esqueleto
arm=bpy.data.armatures.new('esqueleto'); ao=bpy.data.objects.new('esqueleto', arm); col.objects.link(ao)
ao.parent=root; ao.matrix_parent_inverse=Matrix.Identity(4); ao.show_in_front=True
arm.display_type='STICK'
bpy.context.view_layer.objects.active=ao
bpy.ops.object.mode_set(mode='EDIT')
BN={b['k']:b for b in E['bones']}
for b in E['bones']:
    eb=arm.edit_bones.new('h%d'%b['k']); eb.head=Vector(b['head']); eb.tail=Vector(b['tail']); eb.roll=0
for b in E['bones']:
    if b['parent']>=0: arm.edit_bones['h%d'%b['k']].parent=arm.edit_bones['h%d'%b['parent']]
bpy.ops.object.mode_set(mode='OBJECT')
def S4(k, Sd=None):
    s=(Sd or {}).get(str(k)) or BN[k]['S']; m=Matrix.Identity(4)
    for r in range(3):
        for c in range(4): m[r][c]=s[r][c]
    return m
def apply_pose(Sd=None):
    # pose(k) = S(k) * rest(k); basis = (rest_p^-1 rest_k)^-1 * pose_p^-1 * pose_k
    P={}
    for b in E['bones']:
        k=b['k']; rest=arm.bones['h%d'%k].matrix_local; P[k]=S4(k,Sd) @ rest
    for b in E['bones']:
        k=b['k']; pb=ao.pose.bones['h%d'%k]; rest=arm.bones['h%d'%k].matrix_local
        if b['parent']>=0:
            p=b['parent']; rp=arm.bones['h%d'%p].matrix_local
            pb.matrix_basis=(rp.inverted() @ rest).inverted() @ P[p].inverted() @ P[k]
        else:
            pb.matrix_basis=rest.inverted() @ P[k]
apply_pose()
# --- brazo izquierdo (solo los triangulos del brazo izq), con grupos de huesos
A=E['arm']; dom=[max(range(4),key=lambda j:A['w'][i][j]) for i in range(len(A['v']))]
domb=[A['b'][i][dom[i]] for i in range(len(A['v']))]
keepv=[b in LEFT for b in domb]
T=[t for t in A['t'] if all(keepv[i] for i in t)]
used=sorted(set(i for t in T for i in t)); remap={o:n for n,o in enumerate(used)}
mo=mkmesh('mano_izq',[A['v'][i] for i in used],[[remap[i] for i in t] for t in T],(0.85,0.62,0.45,1),ao)
for i,o in enumerate(used):
    for j in range(4):
        w=A['w'][o][j]
        if w<=0: continue
        g='h%d'%A['b'][o][j]
        vg=mo.vertex_groups.get(g) or mo.vertex_groups.new(name=g); vg.add([i],w,'ADD')
md=mo.modifiers.new('esqueleto','ARMATURE'); md.object=ao
# --- arma (fija), cargador y balas (se mueven juntos)
mkmesh('arma_no_tocar',E['gun']['v'],E['gun']['t'],(0.25,0.25,0.25,1),root).hide_select=True
mag=bpy.data.objects.new('cargador',None); col.objects.link(mag); mag.parent=root; mag.matrix_parent_inverse=Matrix.Identity(4)
mag.empty_display_type='ARROWS'; mag.empty_display_size=0.03
mkmesh('cargador_cuerpo',E['mag']['v'],E['mag']['t'],(0.1,0.2,0.7,1),mag)
mkmesh('cargador_balas',E['bul']['v'],E['bul']['t'],(0.8,0.65,0.2,1),mag)
def set_mag(D):
    m=Matrix.Identity(4)
    for r in range(4):
        for c in range(4): m[r][c]=D[r][c]
    mag.matrix_basis=m
pose_file=os.path.join(TMP,'metro_mano_pose.json')
if os.path.exists(pose_file):
    Pz=json.load(open(pose_file)); apply_pose(Pz.get('S')); 
    if 'D_mag' in Pz: set_mag(Pz['D_mag'])
bpy.context.view_layer.update()
# vista: encuadrar la mano
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        sp=area.spaces.active; sp.shading.type='SOLID'; sp.shading.color_type='MATERIAL'; sp.overlay.show_floor=False
print('METRO: escena lista')
