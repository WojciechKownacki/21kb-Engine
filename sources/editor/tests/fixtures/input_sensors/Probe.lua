-- @asset /Game/Input/Motion.21kbinputrecording
local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==30 then
  assert(CallFunction("Input.ReplayFrame",{asset="/Game/Input/Motion.21kbinputrecording",frame=0}))
  local a=CallFunction("Input.Sensor",{kind="Accelerometer"})
  local g=CallFunction("Input.Sensor",{kind="Gyroscope"})
  assert(a.available and g.available and a.replayed and g.replayed and a.source=="Replay")
  assert(a.unit=="m/s^2" and g.unit=="rad/s" and math.abs(a.y-9.80665)<.00001 and math.abs(g.z-1.57079632679)<.00001)
  assert(a.timeNanoseconds==1000000000)
 end
 if frame==40 then
  assert(CallFunction("Input.ReplayFrame",{asset="/Game/Input/Motion.21kbinputrecording",frame=1}))
  local a=CallFunction("Input.Sensor",{kind="Accelerometer"})
  assert(not a.available and a.status=="Suspended" and a.y==0 and a.timeNanoseconds==0 and a.replayed)
 end
 if frame==70 then Log("lua-input-sensors PASS") end
end
