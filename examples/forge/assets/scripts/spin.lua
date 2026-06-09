-- Spins the entity around an axis over time. Exposed fields show up in the inspector
-- and can be tuned live; editing this file hot-reloads the behavior while playing.
fields = {
    speed = 60.0,     -- degrees per second
    yaw = true,       -- spin around Y
    pitch = false,    -- spin around X
}

function on_start(self)
    -- nothing to set up; on_update does the work
end

function on_update(self, dt)
    local step = fields.speed * dt
    local x = 0.0
    local y = 0.0
    if fields.pitch then x = step end
    if fields.yaw then y = step end
    self:rotate(x, y, 0.0)
end
