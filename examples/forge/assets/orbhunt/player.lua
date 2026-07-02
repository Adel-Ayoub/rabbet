fields = { speed = 7.0 }

function on_update(self, dt)
  local vx, vy, vz = self:velocity()
  local x, z = 0.0, 0.0
  if input.down("W") then z = z - 1.0 end
  if input.down("S") then z = z + 1.0 end
  if input.down("A") then x = x - 1.0 end
  if input.down("D") then x = x + 1.0 end
  local len = math.sqrt(x * x + z * z)
  if len > 0.0 then
    self:set_velocity(x / len * fields.speed, vy, z / len * fields.speed)
  end
end
