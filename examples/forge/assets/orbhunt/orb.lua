fields = { radius = 1.8 }

function on_update(self, dt)
  local player = world.find("Player")
  if player == nil then return end
  if self:distance_to(player) < fields.radius then
    local x, y, z = self:position()
    world.spawn("pickup_flash", x, y, z)
    world.destroy(self)
  end
end
