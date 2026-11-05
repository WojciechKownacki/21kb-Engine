-- @asset /Game/Compiled/Union/Geometry.21kb
-- @asset /Game/Compiled/Union/Geometry.kbcollision
local frame=0
function Ready(self) frame=0 end
function Tick(self,dt)
 frame=frame+1
 if frame==5 then assert(CSG.Attach(self.entity,"/Game/Compiled/Union/Geometry.21kb","/Game/Compiled/Union/Geometry.kbcollision")) end
 if frame==50 then Log("lua-csg PASS union") end
end
