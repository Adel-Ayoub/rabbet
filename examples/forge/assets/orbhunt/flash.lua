age = 0.0

function on_update(self, dt)
  age = age + dt
  if age > 2.5 then
    world.destroy(self)
  end
end
