fields = { speed = 2.2, reach = 1.4, catch = 1.5 }

-- Drifts at the height it was spawned at: the arena floor is flat, so a horizontal
-- chase keeps the wisp at lantern height without sampling terrain.
function on_update(self, dt)
  local player = world.find("Player")
  if player ~= nil and self:distance_to(player) < fields.catch then
    local x, y, z = self:position()
    world.spawn("wisp_pop", x, y, z)
    world.destroy(self)
    return
  end

  local lamp = world.find("Lamp Post")
  if lamp == nil then return end

  local x, y, z = self:position()
  local lx, _, lz = lamp:position()
  local dx, dz = lx - x, lz - z
  local flat = math.sqrt(dx * dx + dz * dz)

  if flat < fields.reach then
    -- One flame per wisp. find is first-match, so two wisps landing on the same tick
    -- both name the same flame and only one dies, which errs in the player's favour.
    local flame = world.find("Flame")
    if flame ~= nil then
      local fx, fy, fz = flame:position()
      world.spawn("flame_pop", fx, fy, fz)
      world.destroy(flame)
    end
    world.destroy(self)
    return
  end

  local step = fields.speed * dt
  self:set_position(x + dx / flat * step, y, z + dz / flat * step)
end
