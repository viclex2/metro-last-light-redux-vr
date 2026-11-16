import bpy
from mathutils import Vector, Matrix
def ver(d=(0,-1,0.3), dist=0.35, centro=None, ortho=False):
    ao=bpy.data.objects['esqueleto']
    if centro is None:
        mo=bpy.data.objects['mano_izq']; dg=bpy.context.evaluated_depsgraph_get(); me=mo.evaluated_get(dg).to_mesh()
        vs=[mo.matrix_world@v.co for v in me.vertices]
        # centro = zona de la mano (huesos de la mano): media de los vertices mas lejanos del hombro
        sh=ao.matrix_world@ao.pose.bones['h63'].head
        vs.sort(key=lambda v:(v-sh).length); vs=vs[int(len(vs)*0.7):]
        centro=sum(vs,Vector())/len(vs)
    d=Vector(d).normalized()
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            r3=area.spaces.active.region_3d
            r3.view_location=centro; r3.view_distance=dist
            r3.view_rotation=(-d).to_track_quat('-Z','Y')
            r3.view_perspective='ORTHO' if ortho else 'PERSP'
            area.spaces.active.lens=50; area.spaces.active.clip_start=0.001
    return centro
