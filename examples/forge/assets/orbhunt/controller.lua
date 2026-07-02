won = false

function on_update(self, dt)
  if won then return end
  if world.find("Orb") == nil then
    won = true
    local x, y, z = self:position()
    world.spawn("victory", x, y, z)
  end
end
