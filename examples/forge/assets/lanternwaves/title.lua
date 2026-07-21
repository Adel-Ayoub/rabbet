fields = { attract = 20.0 }

-- Space or Enter starts the run; left alone the title drops into the arena on its own so
-- the demo keeps playing itself on a showcase machine (and so the entry transition is
-- reachable headlessly, where there is no input device to press anything).
idle = 0.0
gone = false

function on_update(self, dt)
  if gone then return end
  idle = idle + dt
  if input.pressed("Space") or input.pressed("Enter") or idle > fields.attract then
    gone = true
    world.load_scene("arena")
  end
end
