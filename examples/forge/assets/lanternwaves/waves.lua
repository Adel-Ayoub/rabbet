fields = {
  waves = 3.0,
  per_wave = 4.0,
  spawn_gap = 1.1,
  wave_gap = 5.0,
  lead_in = 2.0,
  radius = 20.0,
  height = 1.4,
  settle = 3.0
}

-- timer stays nil until the first update: the chunk runs before the component's field
-- overrides land, so anything derived from fields has to wait for a tick.
timer = nil
wave = 0
left = 0
spawned = 0
ending = nil
ending_age = 0.0

function on_update(self, dt)
  if timer == nil then timer = fields.lead_in end

  if ending ~= nil then
    ending_age = ending_age + dt
    if ending_age > fields.settle then
      world.load_scene("title")
    end
    return
  end

  -- The last flame is out: the wisps took the lantern.
  if world.find("Flame") == nil then
    ending = "lost"
    ending_age = 0.0
    return
  end

  timer = timer - dt

  if left > 0 then
    if timer <= 0.0 then
      -- Golden-angle stepping, so a wave arrives from spread-out bearings without
      -- math.random (which Lua seeds unpredictably and would desync the test).
      local cx, _, cz = self:position()
      local a = spawned * 2.3999632
      world.spawn("wisp", cx + math.cos(a) * fields.radius, fields.height,
                  cz + math.sin(a) * fields.radius)
      spawned = spawned + 1
      left = left - 1
      -- <= rather than ==: per_wave is an inspector-editable number, and a fractional
      -- one would step straight past zero and spawn waves forever.
      if left <= 0 then
        wave = wave + 1
        timer = fields.wave_gap
      else
        timer = fields.spawn_gap
      end
    end
    return
  end

  if wave < fields.waves then
    if timer <= 0.0 then
      left = fields.per_wave
      timer = 0.0 -- the wave's first wisp goes out next tick
    end
    return
  end

  -- Every wave has been spawned; the lantern survives once the field is clear.
  if world.find("Wisp") == nil then
    ending = "won"
    ending_age = 0.0
    -- Hangs above the lantern head (world y 1.95) rather than inside it.
    local x, y, z = self:position()
    world.spawn("triumph", x, y - 0.6, z)
  end
end
