import bpy,json,sys
from pathlib import Path
names=['ShaderNodeHalfLambert','ShaderNodeEdgeOutline','ShaderNodeEdgeRim','ShaderNodeSDFDriver','ShaderNodeNPRShadowCaster','ShaderNodeNPRCharacterShadow','ShaderNodeEyeSeeThrough','ShaderNodeEyeSeeThroughSource']
m=bpy.data.materials.new('PackageSmokeTest');m.use_nodes=True
results={}
for name in names:
    try:results[name]=m.node_tree.nodes.new(name).bl_idname==name
    except Exception as e:results[name]=str(e)
data={'version':bpy.app.version_string,'binary':Path(bpy.app.binary_path).name,'nodes':results,'all_passed':all(v is True for v in results.values())}
Path(__file__).with_name('SMOKE_TEST.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
print('PACKAGE_SMOKE',json.dumps(data))
if not data['all_passed']:raise RuntimeError('Node smoke test failed')
