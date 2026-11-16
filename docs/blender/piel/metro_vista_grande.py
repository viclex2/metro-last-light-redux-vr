import bpy
for area in bpy.context.screen.areas:
    if area.type=='TEXT_EDITOR':
        area.type='VIEW_3D'
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        sp=area.spaces.active; sp.shading.type='SOLID'; sp.shading.color_type='MATERIAL'; sp.overlay.show_floor=False; sp.overlay.show_axis_x=False; sp.overlay.show_axis_y=False
        sp.shading.light='STUDIO'
exec(open(bpy.path.abspath(r'C:\Users\vicle\AppData\Local\Temp\metro_ver.py')).read())
