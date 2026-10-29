"""Exercise the cooked WebGPU player and compute on its actual browser device."""
import time

from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import require


def author_fixture(args, root):
    def configure(run, component, project):
        settings = ["effect.backendPolicy=GpuVisualPreferred", "effect.determinismSeed=42",
            "effect.emitter[0].maxParticles=64", "effect.emitter[0].spawn.mode=Burst",
            "effect.emitter[0].spawn.burstCount=1", "effect.emitter[0].spawn.burst[0].timeSeconds=0",
            "effect.emitter[0].spawn.burst[0].count=16", "effect.emitter[0].spawn.lifetimeMin=30",
            "effect.emitter[0].spawn.lifetimeMax=30", "effect.emitter[0].output.softParticles=false"]
        run(args.cli, "vfx-new", "--project", project, "--name", "ComputeEffect",
            *[argument for value in settings for argument in ("--set", value)])
        run(args.cli, "entity", "add", "--project", project, "--name", "ComputeEffect")
        component("ComputeEffect", "Particle Effect", "effectAssetId=/Game/Particles/ComputeEffect.kbvfx")
        run(args.cli, "component", "set", "--project", project, "--entity", "MainCamera",
            "--type", "Transform", "--set", "localPosition.z=-10")
    return author_storage(args, root, target="WebGPU.wasm32", configure=configure)


OBSERVER = """
window.kbGpuObservation={devices:0,pipelines:0,draws:0,dispatches:0,errors:[],lost:[]};
if(globalThis.GPUAdapter){
  const request=GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice=async function(...args){
    const device=await request.apply(this,args);
    kbGpuObservation.adapter={vendor:this.info?.vendor,architecture:this.info?.architecture,
      device:this.info?.device,description:this.info?.description,isFallbackAdapter:this.info?.isFallbackAdapter};
    ++kbGpuObservation.devices;
    window.kbActualEngineDevice=device;
    device.addEventListener('uncapturederror',event=>kbGpuObservation.errors.push(event.error.message));
    device.lost.then(info=>kbGpuObservation.lost.push({reason:info.reason,message:info.message}));
    return device;
  };
  const pipeline=GPUDevice.prototype.createRenderPipeline;
  GPUDevice.prototype.createRenderPipeline=function(...args){
    const result=pipeline.apply(this,args);++kbGpuObservation.pipelines;return result;
  };
  for(const name of ['draw','drawIndexed','drawIndirect','drawIndexedIndirect']){
    const draw=GPURenderPassEncoder.prototype[name];
    GPURenderPassEncoder.prototype[name]=function(...args){++kbGpuObservation.draws;return draw.apply(this,args);};
  }
  for(const name of ['dispatchWorkgroups','dispatchWorkgroupsIndirect']){
    const dispatch=GPUComputePassEncoder.prototype[name];
    GPUComputePassEncoder.prototype[name]=function(...args){++kbGpuObservation.dispatches;return dispatch.apply(this,args);};
  }
}
"""

COMPUTE = """
(async()=>{
  const d=window.kbActualEngineDevice;
  d.pushErrorScope('validation');
  const shader=d.createShaderModule({code:`
    @group(0) @binding(0) var<storage,read_write> output:array<u32>;
    @compute @workgroup_size(8) fn main(@builtin(global_invocation_id) id:vec3u){
      output[id.x]=id.x*id.x+7u;
    }`});
  const pipeline=d.createComputePipeline({layout:'auto',compute:{module:shader,entryPoint:'main'}});
  const output=d.createBuffer({size:32,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC});
  const readback=d.createBuffer({size:32,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
  try{
    const bindings=d.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[{binding:0,resource:{buffer:output}}]});
    const commands=d.createCommandEncoder();
    const pass=commands.beginComputePass();pass.setPipeline(pipeline);pass.setBindGroup(0,bindings);
    pass.dispatchWorkgroups(1);pass.end();commands.copyBufferToBuffer(output,0,readback,0,32);
    d.queue.submit([commands.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    const values=Array.from(new Uint32Array(readback.getMappedRange()));readback.unmap();
    const error=await d.popErrorScope();return {values,error:error?.message||null};
  }finally{output.destroy();readback.destroy();}
})()
"""


class WebGpuScenario(StorageScenario):
    def graphics(self):
        state = self.client.evaluate("({backend:document.documentElement.dataset.kbBackend,...kbGpuObservation})")
        require(state["backend"] == "webgpu" and state["devices"] > 0 and state["pipelines"] > 0
            and state["draws"] > 0, "No actual WebGPU rendering: " + str(state))
        require(not state["errors"] and not state["lost"], "GPU errors: " + str(state))
        require(not state["adapter"].get("isFallbackAdapter"), "Software fallback adapter selected")
        return state


def wait_error(case, code):
    deadline = time.monotonic() + 25
    while True:
        state = case.client.evaluate("({...document.documentElement.dataset})")
        if state.get("kbError") == code:
            case.report["observations"].append(dict(check=code, state=state))
            return
        require(time.monotonic() < deadline, "Expected " + code + ": " + str(state))
        time.sleep(.03)


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "webgpu", OBSERVER,
            scenario_type=WebGpuScenario) as case:
        case.ready()
        case.blue_button("actual-webgpu-ui")
        report["webgpu"] = case.graphics()
        require(report["webgpu"]["dispatches"] > 0,
            "Cooked particle simulation did not dispatch through the engine's WebGPU backend")
        result = case.client.call("Runtime.evaluate", expression=COMPUTE, awaitPromise=True, returnByValue=True)
        require("exceptionDetails" not in result, "Compute threw: " + str(result))
        value = result["result"].get("value")
        require(value and not value["error"] and value["values"] == [i*i+7 for i in range(8)],
            "Actual GPU compute readback mismatch: " + str(value))
        report["observations"].append(dict(check="compute on the actual engine device", result=value))
        # This browser and its GPU process belong to this fresh test profile.
        case.client.call("Browser.crashGpuProcess")
        wait_error(case, "graphics-device-lost")

    for name, script, error in [
        ("missing", "Object.defineProperty(navigator,'gpu',{value:undefined});", "webgpu-unavailable"),
        ("adapter-null", "navigator.gpu.requestAdapter=async()=>null;", "webgpu-adapter"),
        ("adapter-rejected", "navigator.gpu.requestAdapter=async()=>{throw new Error('adapter denied')};", "webgpu-adapter"),
        ("device-rejected", "GPUAdapter.prototype.requestDevice=async()=>{throw new Error('device denied')};", "renderer-init"),
    ]:
        with BrowserSession(args, root, root / ("profile-" + name), report, origin, name, script,
                scenario_type=WebGpuScenario) as case:
            wait_error(case, error)
            require(not case.client.evaluate("document.documentElement.dataset.kbFirstFrame"),
                "Failed startup reported a rendered first frame")


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-webgpu-tests/v1", description=__doc__, prefix="GameWebGpu"))
