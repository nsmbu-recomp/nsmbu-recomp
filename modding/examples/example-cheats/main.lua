local ____exports = {}
local heldMode = {}
local prevSpeedY = {}
local scaledJump = {}
local function freezeTimer()
	nsmbu.Level.set_time(999)
end
local function keepLives()
	for ____, p in ipairs(nsmbu.Player.get_players()) do
		local lives = p:lives()
		if lives ~= nil and lives > 0 and lives < 99 then
			p:set_lives(99)
		end
	end
end
local function keepPowerup()
	do
		local i = 0
		while i < 4 do
			local p = nsmbu.Player.get_player(i)
			local entered = false
			if p then
				local value = p:entered()
				entered = value == true
			end
			if p and entered then
				local mode = p:mode()
				if mode ~= nil then
					local held = heldMode[i]
					if held ~= nil and held >= mode then
						if mode < held and held > 0 then
							p:set_mode(held)
						end
					else
						heldMode[i] = mode
					end
				end
			else
				heldMode[i] = nil
			end
			i = i + 1
		end
	end
end
local function scaleJump(p, i, scale)
	local speed = p:speed_y()
	if speed == nil then
		prevSpeedY[i] = nil
		scaledJump[i] = false
		return
	end
	local prev = prevSpeedY[i] or 0
	if speed > 1.5 and prev <= 1.5 then
		scaledJump[i] = false
	elseif speed <= 0 then
		scaledJump[i] = false
	elseif not scaledJump[i] and speed > 1.5 then
		p:set_speed_y(speed * scale)
		scaledJump[i] = true
	end
	prevSpeedY[i] = speed
end
local function scaleJumps(scale)
	do
		local i = 0
		while i < 4 do
			local p = nsmbu.Player.get_player(i)
			if p then
				scaleJump(p, i, scale)
			end
			i = i + 1
		end
	end
end
nsmbu.Hooks.onLoaded = function() end
nsmbu.Hooks.onLogicStep = function(step)
	local infiniteTime = nsmbu.Config.bool("infinite_time", false)
	if infiniteTime then
		freezeTimer()
	end
	local infiniteLives = nsmbu.Config.bool("infinite_lives", false)
	if infiniteLives then
		keepLives()
	end
	local keepPowerupOption = nsmbu.Config.bool("keep_powerup", false)
	if keepPowerupOption then
		keepPowerup()
	end
	local scale = nsmbu.Config.number("jump_scale", 1)
	if scale ~= nil and scale ~= 1 then
		scaleJumps(scale)
	end
end
return ____exports
