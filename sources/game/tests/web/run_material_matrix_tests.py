"""Compare cooked WebGL PBR materials with native capability-reference images."""
from __future__ import annotations
import argparse
import base64
import http.server
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from run_game_storage_tests import BrowserSession
from run_packaged_runtime_tests import require

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from compare_images import main as compare_main
from png_readback import read_png, write_png

OBSERVER = """
(() => {
window.materialGraphics={draws:0,maxTargets:0,incomplete:0,shaderErrors:[],blendDraws:0,renderer:''};
const compile=WebGL2RenderingContext.prototype.compileShader;
WebGL2RenderingContext.prototype.compileShader=function(shader){
  compile.call(this,shader);
  if(!this.getShaderParameter(shader,this.COMPILE_STATUS))
    materialGraphics.shaderErrors.push(this.getShaderInfoLog(shader));
};
for(const name of ['drawArraysInstanced','drawElementsInstanced']){
  const original=WebGL2RenderingContext.prototype[name];
  WebGL2RenderingContext.prototype[name]=function(...args){
    window.materialContext=this;
    const g=materialGraphics;g.draws++;
    if(!g.renderer){const ext=this.getExtension('WEBGL_debug_renderer_info');
      g.rendererAvailable=!!ext;g.renderer=this.getParameter(ext?ext.UNMASKED_RENDERER_WEBGL:this.RENDERER);}
    if(this.getParameter(this.DRAW_FRAMEBUFFER_BINDING)){
      if(this.checkFramebufferStatus(this.DRAW_FRAMEBUFFER)!==this.FRAMEBUFFER_COMPLETE)g.incomplete++;
      let count=0;for(let i=0;i<this.getParameter(this.MAX_DRAW_BUFFERS);i++)
        if(this.getParameter(this.DRAW_BUFFER0+i)!==this.NONE)count++;
      g.maxTargets=Math.max(g.maxTargets,count);
    }
    if(this.isEnabled(this.BLEND))g.blendDraws++;
    return original.apply(this,args);
  };
}
})();
"""


class MaterialScenario:
    def __init__(self, client, root, report):
        self.client, self.root, self.report = client, root, report

    def states(self):
        return []

    def capture(self, name, path):
        self.client.call("Log.enable")
        self.client.call("Emulation.setDeviceMetricsOverride", width=800, height=600,
                         deviceScaleFactor=1, mobile=False)
        deadline = time.monotonic() + 90
        while True:
            state = self.client.evaluate("({data:{...document.documentElement.dataset},graphics:window.materialGraphics||{draws:0}})")
            require(not state["data"].get("kbError"), str(state))
            require(time.monotonic() < deadline, "Web material scene did not become ready")
            if state["data"].get("kbFirstFrame") == "true" and state["graphics"]["draws"] >= 300:
                break
            time.sleep(0.05)
        self.client.call("Runtime.evaluate", expression="new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>resolve(true))))",
                         awaitPromise=True)
        picture = self.client.call("Page.captureScreenshot", format="png",
                                  clip=dict(x=0,y=0,width=800,height=600,scale=1))["data"]
        path.write_bytes(base64.b64decode(picture))
        graphics = self.client.evaluate("materialGraphics")
        require(not graphics["shaderErrors"] and graphics["incomplete"] == 0, str(graphics))
        require(not any(event.get("method")=="Runtime.exceptionThrown" for event in self.client.events),
                "The browser reported an unhandled runtime exception")
        require(graphics.get("rendererAvailable") and bool(graphics["renderer"]),"Hardware renderer identity is unavailable")
        require(not any(word in graphics["renderer"].lower() for word in
                        ["swiftshader", "llvmpipe", "softpipe", "warp", "software", "microsoft basic"]),
                "A hardware WebGL renderer is required: " + str(graphics))
        messages = [str(arg.get("value", "")) for event in self.client.events
                    if event.get("method") == "Runtime.consoleAPICalled"
                    for arg in event["params"].get("args", [])]
        text = "\n".join(messages)
        require("material program fallback:" not in text, "The browser lost a cooked graph program")
        require("packaged lighting path=" + ("2" if name == "deferred" else "0") in text,
                "The browser did not apply the project's lighting path")
        if name == "deferred":
            require(graphics["maxTargets"] == 4, "The portable G-buffer did not use exactly four color targets")
            require("material capability fallback:" in text, "The browser hid its material capability reduction")
        self.report.setdefault("captures", []).append(dict(name=name,graphics=graphics,messages=messages))

    def recreate(self, path):
        injected=self.client.evaluate("(() => {window.lostMaterialContext=window.materialContext;"
            "const ext=lostMaterialContext.getExtension('WEBGL_lose_context');"
            "if(!ext)return false;ext.loseContext();return true;})()")
        require(injected,"Actual WebGL context loss is unavailable")
        deadline=time.monotonic()+20
        while True:
            state=self.client.evaluate("({error:document.documentElement.dataset.kbError,"
                "lost:lostMaterialContext.isContextLost()})")
            if state.get("error")=="graphics-context-lost" and state["lost"]:break
            require(time.monotonic()<deadline,"Material host did not observe actual graphics loss")
            time.sleep(0.05)
        while True:
            state=self.client.evaluate("({error:document.documentElement.dataset.kbError,"
                "replaced:materialContext!==lostMaterialContext,lost:materialContext.isContextLost(),"
                "oldLost:lostMaterialContext.isContextLost()})")
            if not state.get("error") and state["replaced"] and not state["lost"]:break
            require(time.monotonic()<deadline,"Material resources did not recover: "+str(state))
            time.sleep(0.05)
        require(state["oldLost"],"The test did not recreate the graphics context")
        self.client.evaluate("materialGraphics.draws=0")
        self.capture("deferred",path)
        messages=self.report["captures"][-1]["messages"]
        require(sum("packaged lighting path=2" in message for message in messages)==2,
                "The recreated renderer lost the deferred project setting")
        self.report["recreation"]=state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli","cooker","player","engine-root","shaderc","directory","browser","output"]:
        parser.add_argument("--"+name,required=True,type=Path)
    parser.add_argument("--plugin",action="append",type=Path,required=True)
    parser.add_argument("--browser-arg",action="append",default=[])
    parser.add_argument("--prepare-only",action="store_true")
    parser.add_argument("--prepared-root",type=Path)
    args=parser.parse_args()
    for name in ["cli","cooker","player","engine_root","shaderc","directory","browser","output"]:
        setattr(args,name,getattr(args,name).resolve())
    args.output.mkdir(parents=True,exist_ok=True)
    root=args.prepared_root.resolve() if args.prepared_root else Path(tempfile.mkdtemp(prefix="Materials",dir=args.output))
    project=root/"Sample"
    for plugin in args.plugin:
        shutil.copy2(plugin.resolve(),root/plugin.name)
    report=json.loads((root/"result.json").read_text()) if args.prepared_root else dict(result="RUNNING",fixtureVersion=3,commands=[],captures=[],comparisons=[])
    require(report.get("fixtureVersion")==3,"Prepared fixture must include transparent depth coverage")
    if args.prepared_root:
        archive=root/("result-previous-"+str(time.time_ns())+".json")
        shutil.copy2(root/"result.json",archive)
        for key in ["error","regions","browsers","graphicsContexts","cleanupErrors","recreation"]:
            report.pop(key,None)
        report.update(result="RUNNING",captures=[],comparisons=[])
    def save():
        (root/"result.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    def run(name,*command):
        env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']='2'
        with (root/(name+".log")).open("w",encoding="utf-8") as log:
            result=subprocess.run(list(map(str,command)),cwd=args.engine_root,env=env,stdout=log,
                stderr=subprocess.STDOUT,timeout=900,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        report["commands"].append(dict(name=name,exitCode=result.returncode));save()
        require(result.returncode==0,"Command failed: "+name)
    def cli(name,*tail):
        run(name,args.cli,*tail)
    server=None
    try:
        if not args.prepared_root:
            cli("new","new","--parent",root,"--name","Sample","--template","Empty")
            profiles=project/"Assets/Profiles";profiles.mkdir(parents=True,exist_ok=True)
            (profiles/"MaterialReference.kbppfx").write_text(
                "bloomEnabled 0\ntemporalAntiAliasingEnabled 0\ntemporalJitterEnabled 0\n"
                "fxaaEnabled 0\ntonemapEnabled 1\nautoExposureMetering Manual\n"
                "outputTransform.autoExposure.enabled 0\noutputTransform.exposureStops 0\n"
                "outputTransform.gamma 2.2\noutputTransform.tonemap Aces\n",encoding="utf-8")
            cli("profile","scene-post-process","--project",project,"--profile","/Game/Profiles/MaterialReference.kbppfx")
            cli("camera","entity","add","--project",project,"--name","Camera","--create","Camera","--position","0,0,-11")
            cli("front","entity","add","--project",project,"--name","Front","--create","Point Light","--position","-3,4,-5")
            cli("front-light","component","set","--project",project,"--entity","Front","--type","Light","--set","intensity=1.2","--set","range=40","--set","castsShadow=false")
            cli("back","entity","add","--project",project,"--name","Back","--create","Point Light","--position","2,-1,3")
            cli("back-light","component","set","--project",project,"--entity","Back","--type","Light","--set","intensity=0.3","--set","range=40","--set","castsShadow=false")
            textures=project/"Assets/Textures";textures.mkdir(parents=True,exist_ok=True)
            # Uncompressed top-down RGBA TGA keeps the mask alpha without an imaging dependency.
            tga=bytearray(18);tga[2]=2;tga[12]=4;tga[14]=4;tga[16]=32;tga[17]=0x28
            for y in range(4):
                for x in range(4):
                    r,g,b,a=(255,40,10,255) if (x+y)%2 else (20,220,80,0)
                    tga.extend((b,g,r,a))
            (textures/"Checker.tga").write_bytes(tga)
            write_png(textures/"Normal.png",[[(200,128,220)]*4 for _ in range(4)])
            write_png(textures/"MetalRough.png",[[(0,128,0)]*4 for _ in range(4)])
            header="version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\n"
            materials={
                "Base":"baseColor 0.45 0.25 0.12 1\n",
                "Metal":"baseColor 0.7 0.7 0.7 1\nmetallicFactor 1\nroughnessFactor 0.2\n",
                "Mapped":"albedoTexture /Game/Textures/Checker.tga\nnormalTexture /Game/Textures/Normal.png\nmetallicRoughnessTexture /Game/Textures/MetalRough.png\nmetallicFactor 1\n",
                "Mask":"albedoTexture /Game/Textures/Checker.tga\nalphaMode MASK\nalphaCutoff 0.5\n",
                "Blend":"baseColor 0.1 0.7 0.9 0.5\nalphaMode BLEND\n",
                "Emissive":"baseColor 0.1 0.1 0.1 1\nemissiveColor 0.1 0.5 0.2\nemissiveStrength 2\n",
                "Coat":"baseColor 0.1 0.04 0.02 1\nclearcoatFactor 1\nclearcoatRoughnessFactor 0.3\n",
                "Scatter":"baseColor 0.02 0.05 0.4 1\nsubsurfaceFactor 0.8\nsubsurfaceColor 0.3 0.02 0.01\n",
                "Combined":"baseColor 0.02 0.05 0.4 1\nsubsurfaceFactor 0.8\nsubsurfaceColor 0.3 0.02 0.01\nclearcoatFactor 1\nclearcoatRoughnessFactor 0.3\n",
                "Tiled":"albedoTexture /Game/Textures/Checker.tga\ntiling 4 4\n",
                "Point":"albedoTexture /Game/Textures/Checker.tga\npointSampling true\ntiling 0 0\noffset 1.25 0.25\n",
            }
            folder=project/"Assets/Materials";folder.mkdir(parents=True,exist_ok=True)
            for name,body in materials.items(): (folder/(name+".kbmat")).write_text(header+("" if "roughnessFactor" in body else "roughnessFactor 0.65\n")+body,encoding="utf-8")
            graph=(args.engine_root/"sources/editor/tests/HeadlessMaterialPreview/Assets/Galeria/M21_Baza.kbmat").read_text(encoding="utf-8")
            graph=graph.replace("graphShadingModel defaultLit","graphShadingModel subsurface").replace("graphParameterValue color4 Color 1 1 1 1","graphParameterValue color4 Color 0.02 0.05 0.4 1")
            graph+="graphParameterValue tint Color 0.3 0.02 0.01 1\ngraphNode 5 ParameterColor 0 480\ngraphParameter 5 tint Tint Core 0.3%200.02%200.01%201 _ _ _ Unknown true 0 _\ngraphLink 4238460239 5 991233 rgba 1 69665 subsurfaceColor\ngraphLink 4238460240 3 139265 value 1 69656 clearCoat\n"
            (folder/"Graph.kbmat").write_text(graph,encoding="utf-8");names=[*materials,"Graph"]
            for index,name in enumerate(names):
                x=(index%3-1)*2.2;y=3.3-(index//3)*2.2
                cli("entity-"+name,"entity","add","--project",project,"--name",name,"--create","Sphere","--position",f"{x},{y},0")
                cli("scale-"+name,"component","set","--project",project,"--entity",name,"--type","Transform","--set","localScale=1.8,1.8,1.8")
                cli("material-"+name,"component","set","--project",project,"--entity",name,"--type","MeshRenderer","--set",f"material=/Game/Materials/{name}.kbmat")
            # The blend sphere must occlude the rear cube and be occluded by the front cube.
            for name,x,z in [("FrontOccluder",-0.4,-1.0),("RearOccluder",0.4,1.0)]:
                cli("entity-"+name,"entity","add","--project",project,"--name",name,"--create","Cube","--position",f"{x},1.1,{z}")
                cli("scale-"+name,"component","set","--project",project,"--entity",name,"--type","Transform","--set","localScale=0.4,0.6,0.4")
                cli("material-"+name,"component","set","--project",project,"--entity",name,"--type","MeshRenderer","--set","material=/Game/Materials/Base.kbmat")
            stage=root/"Stage";stage.mkdir()
            for name in ["forward","deferred"]:
                settings=project/"Config/ProjectSettings.ini"
                text=settings.read_text(encoding="utf-8")
                import re
                text=re.sub(r"LightingPath=\w+","LightingPath="+name.capitalize(),text);settings.write_text(text,encoding="utf-8")
                web=stage/name;web.mkdir()
                for source in args.directory.glob("Game.*"):
                    if source.suffix != ".kbpack": shutil.copy2(source,web/source.name)
                run("cook-web-"+name,args.cooker,"--project",project,"--target","WebGL.wasm32","--output",web/"Game.kbpack","--engine-root",args.engine_root,"--shaderc",args.shaderc,"--cache",args.output/"CookCache")
                require("variant=GBufferCompact" in (root/("cook-web-"+name+".log")).read_text(encoding="utf-8"),"Portable cook did not report the selected variant")
                cook_log=(root/("cook-web-"+name+".log")).read_text(encoding="utf-8")
                for material in ["Scatter","Combined"]:
                    require(material+".kbmat backend=essl variant=GBuffer;" in cook_log,
                            "Portable cook hid the built-in layer reduction: "+material)
                control=root/("Control-"+name);shutil.copytree(project,control)
                if name=="deferred":
                    for material in ["Scatter","Combined"]:
                        p=control/"Assets/Materials"/(material+".kbmat");text=p.read_text()
                        text=text.replace("subsurfaceColor 0.3 0.02 0.01","subsurfaceColor 0.02 0.05 0.4")
                        if material=="Combined":text=text.replace("subsurfaceFactor 0.8","subsurfaceFactor 0")
                        p.write_text(text)
                    p=control/"Assets/Materials/Graph.kbmat";p.write_text(p.read_text().replace("graphParameterValue tint Color 0.3 0.02 0.01 1","graphParameterValue tint Color 0.02 0.05 0.4 1"))
                native=root/(name+".kbpack")
                run("cook-native-"+name,args.cooker,"--project",control,"--target","Windows.x64","--output",native,"--engine-root",args.engine_root,"--shaderc",args.shaderc,"--cache",args.output/"CookCache")
                run("native-"+name,args.player,f"--project={native}","--headless","--uncapped","--profile-fixed-step",f"--profile-file={root/(name+'-profile.csv')}","--renderer=d3d11","--width=800","--height=600","--frames=124","--screenshot-frame=120",f"--screenshot-file={root/(name+'-native.png')}")
            # A saturated reference can hide an incorrect BRDF or a missing texture.
            scale=600/(2*math.tan(math.radians(30))*11)
            for name in ["forward","deferred"]:
                rows=read_png(root/(name+"-native.png"))
                cx=round(400-2.2*scale);cy=round(300-3.3*scale)
                pixels=[rows[y][x] for y in range(cy-40,cy+40) for x in range(cx-40,cx+40)]
                require(sum(min(pixel)>=250 for pixel in pixels)<len(pixels)*0.05,
                        "The base-material reference is saturated: "+name)
                require(sum(pixel[0]>pixel[2]+8 for pixel in pixels)>100,
                        "The base-material reference lost its authored color: "+name)
            forward,deferred=read_png(root/"forward-native.png"),read_png(root/"deferred-native.png")
            cx=400;cy=round(300-1.1*scale)
            deltas=[abs(forward[y][x][c]-deferred[y][x][c])
                    for y in range(cy-48,cy+48) for x in range(cx-48,cx+48) for c in range(3)]
            report["nativeTransparentDepthMean"]=sum(deltas)/len(deltas)
            require(report["nativeTransparentDepthMean"]<=3,"Deferred transparent occlusion differs from forward")
            report["materials"]=names
            if args.prepare_only:
                report["result"]="PREPARED"
                print(json.dumps(dict(result="PREPARED",root=str(root))))
                return
        names=report["materials"]
        stage=root/"Stage"
        # Re-stage the freshly built host when authoring/cooking ran before its build.
        for name in ["forward","deferred"]:
            for source in args.directory.glob("Game.*"):
                if source.suffix!=".kbpack":shutil.copy2(source,stage/name/source.name)
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
        server=http.server.ThreadingHTTPServer(("127.0.0.1",0),lambda *values:Quiet(*values,directory=str(stage)))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        origin=f"http://127.0.0.1:{server.server_port}"
        for name in ["forward","deferred"]:
            with BrowserSession(args,root,root/("profile-"+name),report,origin,name,script=OBSERVER,route="/"+name,scenario_type=MaterialScenario) as case:
                case.capture(name,root/(name+"-web.png"))
                if name=="deferred":
                    case.recreate(root/"deferred-recreated.png")
                    restored=compare_main([str(root/"deferred-web.png"),str(root/"deferred-recreated.png"),
                        "--pixel-tolerance","1","--max-mean","0.1","--max-differing","0.001",
                        "--report",str(root/"recreation-comparison.json")])
                    require(restored==0,"Graphics recreation changed the material image")
            result=compare_main([str(root/(name+"-native.png")),str(root/(name+"-web.png")),"--pixel-tolerance","12","--max-mean","2","--max-differing","0.01","--diff",str(root/(name+"-diff.png")),"--report",str(root/(name+"-comparison.json"))])
            report["comparisons"].append(dict(name=name,exitCode=result));save()
            require(result==0,"Web material image differs from its capability reference: "+name)
            a,b=read_png(root/(name+"-native.png")),read_png(root/(name+"-web.png"))
            scale=600/(2*math.tan(math.radians(30))*11)
            for index,material in enumerate(names):
                cx=round(400+(index%3-1)*2.2*scale);cy=round(300-(3.3-(index//3)*2.2)*scale)
                deltas=[abs(a[y][x][c]-b[y][x][c]) for y in range(cy-48,cy+48) for x in range(cx-48,cx+48) for c in range(3)]
                mean=sum(deltas)/len(deltas)
                report.setdefault("regions",[]).append(dict(path=name,material=material,mean=mean,maximum=max(deltas)))
                save()
                require(mean<=3,f"{name}/{material} differs inside its own image region: {mean}")
        report["result"]="PASS"
    except BaseException as error:
        report.update(result="FAIL",error=str(error));raise
    finally:
        if server:server.shutdown();server.server_close()
        save()
    print(json.dumps(dict(result=report["result"],root=str(root))))


if __name__=="__main__":main()




