function OnUIClicked(self, event)
    local action = event.args.action
    local pages = CallFunction("World.FindByName", {name="MenuPages"})
    if action == "Menu.Settings" then
        assert(CallFunction("World.SetPropertyUInt32", {entity=pages, component="Widget Switcher", property="visibleChildIndex", value=1}))
        Log("Menu settings opened")
    elseif action == "Menu.Home" then
        assert(CallFunction("World.SetPropertyUInt32", {entity=pages, component="Widget Switcher", property="visibleChildIndex", value=0}))
        Log("Menu home opened")
    elseif action == "Menu.Play" then
        self.launch = true
        self.progress = 0
        Log("Menu launch confirmed")
    end
end
function Tick(self, dt)
    self.elapsed = (self.elapsed or 0) + dt
    assert(CallFunction("World.SetPropertyFloat", {entity=self.entity, component="Canvas Group", property="opacity", value=0.96+0.04*math.sin(self.elapsed*2)}))
    if self.launch then
        self.progress = math.min(1, self.progress+dt*0.5)
        local progress = CallFunction("World.FindByName", {name="LaunchProgress"})
        assert(CallFunction("World.SetPropertyFloat", {entity=progress, component="Progress Bar", property="value", value=self.progress}))
    end
end
