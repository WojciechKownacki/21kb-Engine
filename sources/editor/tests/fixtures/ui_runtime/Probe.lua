-- @asset /Game/UI/Clip.21kb
local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==6 then
  local first=CallFunction("Accessibility.Node",{index=0});assert(first.count>=4)
  assert(Accessibility.Focus(World.FindByName("Action")))
  assert(Accessibility.Invoke(World.FindByName("Action")))
 end
 if frame==40 then assert(UIVideo.Play(World.FindByName("LuaVideo"),"/Game/UI/Clip.21kb",true)) end
 if frame==50 then
  assert(UIVideo.Pause(World.FindByName("LuaVideo")))
  assert(UIVideo.Seek(World.FindByName("LuaVideo"),1.5))
  local info=CallFunction("UIVideo.Info",{entity=World.FindByName("LuaVideo")});assert(info.available and info.paused and info.videoSeconds>=1.4)
 end
 if frame==70 then Log("lua-runtime-ui PASS") end
end
