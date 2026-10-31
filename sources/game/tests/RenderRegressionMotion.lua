-- Deterministic scene motion shared by editor Play and packaged captures.
local elapsed = 0
function Tick(self, dt)
    elapsed = elapsed + dt
    Transform.SetPosition(self.entity, -2.2 + 0.7 * math.sin(elapsed), 0.5, 0)
end
