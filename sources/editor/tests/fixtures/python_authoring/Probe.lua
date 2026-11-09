function Ready(self)
 local cube=CallFunction("World.FindByName",{name="GeneratedCube"})
 assert(cube ~= nil)
 assert(CallFunction("World.GetPropertyFloat",{entity=cube,component="Transform",property="localPosition.x"})==1.25)
 Log("lua-python-content PASS")
end
