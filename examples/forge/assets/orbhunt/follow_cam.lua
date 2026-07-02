fields = { height = 6.0, back = 9.0 }

function on_update(self, dt)
  local player = world.find("Player")
  if player == nil then return end
  local x, y, z = player:position()
  self:set_position(x, y + fields.height, z + fields.back)
end
