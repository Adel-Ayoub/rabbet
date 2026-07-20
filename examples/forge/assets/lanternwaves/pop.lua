age = 0.0

function on_update(self, dt)
  age = age + dt
  if age > 1.6 then
    world.destroy(self)
  end
end
