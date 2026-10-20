local ____exports = {}
local PROFILE_IDS = {
	coin = 395,
	goomba = 467,
	mushroom = 591,
	["fire flower"] = 592,
	["one-up"] = 597,
	star = 853,
}
local spawned = 0
local spawnedFailed = 0
local function spawnId()
	local name = nsmbu.Config.string("spawn_kind", "off")
	if name == nil or name == "off" then
		return nil
	end
	return PROFILE_IDS[name]
end
local function spawnCountdown(step)
	local every = nsmbu.Config.number("spawn_every", 2)
	return step % math.floor(math.max(every or 1, 1) * 30) == 0
end
local function spawnNearPlayer()
	local id = spawnId()
	if id == nil then
		return
	end
	local p = nsmbu.Player.get_player(0)
	if not p then
		return
	end
	local entered = p:entered()
	if not entered then
		return
	end
	local pos = p:pos()
	if not pos then
		return
	end
	local offsetX = nsmbu.Config.number("spawn_offset_x", 0)
	local offsetY = nsmbu.Config.number("spawn_offset_y", 0)
	local done, err =
		nsmbu.Level.spawn(id, pos.x + (offsetX or 0), pos.y + (offsetY or 0) + 32, pos.z)
	if done then
		spawned = spawned + 1
	else
		spawnedFailed = spawnedFailed + 1
		if spawnedFailed == 1 then
			nsmbu.log(err or "spawn failed")
		end
	end
end
local function clearKindActors()
	local id = spawnId()
	if id == nil then
		return
	end
	for ____, a in ipairs(nsmbu.Level.actors()) do
		local pid = a:profile_id()
		if pid == id then
			a:delete()
		end
	end
end
nsmbu.Hooks.onLogicStep = function(step)
	if not nsmbu.Level.active() then
		return
	end
	local clearKind = nsmbu.Config.bool("clear_kind", false)
	if clearKind then
		clearKindActors()
	end
	if spawnCountdown(step) then
		spawnNearPlayer()
		local count = nsmbu.Level.count()
		nsmbu.status(string.format("spawned %d, live %d", spawned, count or 0))
	end
end
nsmbu.Hooks.onUnload = function()
	nsmbu.log_int("spawned", spawned)
end
return ____exports
