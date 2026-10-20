local ____exports = {}
local Factory = nsmbu.Scene.Factory
local done = false
local opened = false
local requested = false
local last = ""
local logicStep = 0
local pendingPopup = false
local pendingApply = false
local pendingLoad = false
local pendingCourseUp = false
local fileSelectSteps = 0
local popupPulses = 0
local pressErrLogged = false
local function note(step, message)
	if message == last then
		return
	end
	last = message
	nsmbu.log(string.format("intro-skip step %d %s", step, message))
end
local function phaseText()
	local phase = nsmbu.Scene.phase()
	if phase == nil then
		return "none"
	end
	return string.format("%08X", phase)
end
local function saveIndex()
	local picked = nsmbu.Config.string("save_file", "1")
	if picked == "2" then
		return 1
	elseif picked == "3" then
		return 2
	end
	return 0
end
local function coursePack()
	local pack = nsmbu.Config.string("course_pack", "1-1")
	if pack == nil or pack == "" then
		return "1-1"
	end
	return pack
end
local function sceneBusy(err)
	if err == nil then
		return false
	end
	local found = string.find(err, "scene busy", 1, true)
	return found ~= nil
end
local function tryLoadPack(step)
	local pack = coursePack()
	local ok, err = nsmbu.Scene.load_pack(pack)
	if ok == true then
		requested = true
		pendingLoad = false
		note(step, string.format("load_pack %s", pack))
		return
	end
	if sceneBusy(err) then
		pendingLoad = true
	else
		pendingLoad = true
		note(step, string.format("load_pack %s %s", pack, err or "refused"))
	end
end
local function tryApplySave(step)
	local save = saveIndex()
	local ok, err = nsmbu.Scene.apply_save(save)
	if ok == true then
		opened = true
		pendingApply = false
		pendingPopup = false
		pendingLoad = true
		fileSelectSteps = 0
		popupPulses = 0
		note(step, string.format("apply save %d", save + 1))
		return
	end
	if err == "file not ready" or sceneBusy(err) then
		pendingApply = true
	else
		pendingApply = true
		note(step, string.format("apply_save %s phase=%s", err or "refused", phaseText()))
	end
end
nsmbu.Hooks.onSceneChange = function(factory)
	if done then
		return
	end
	if factory == Factory.SaveSelect then
		note(logicStep, "unexpected challenge select")
		done = true
		return
	end
	if factory == Factory.CourseTask then
		if requested then
			pendingCourseUp = true
		end
		return
	end
	if factory == Factory.FileSelect then
		pendingPopup = true
		pendingApply = true
		fileSelectSteps = 0
		popupPulses = 0
		return
	end
	if factory == Factory.Boot then
		note(logicStep, "wait boot")
		return
	end
	if not opened then
		pendingApply = true
		return
	end
	if not requested then
		pendingLoad = true
	end
end
nsmbu.Hooks.onLogicStep = function(step)
	logicStep = step
	if done then
		return
	end
	if pendingCourseUp or requested then
		local courseUp = pendingCourseUp
		if not courseUp then
			local inCourseTask = nsmbu.Scene.course_task()
			courseUp = inCourseTask == true
		end
		if courseUp then
			note(step, "course up")
			done = true
			return
		end
	end
	if not pendingPopup and not pendingApply and not pendingLoad then
		return
	end
	if pendingPopup then
		local inFileSelect = nsmbu.Scene.file_select()
		if not inFileSelect then
			pendingPopup = false
			fileSelectSteps = 0
			popupPulses = 0
		else
			fileSelectSteps = fileSelectSteps + 1
			if popupPulses < 4 then
				if fileSelectSteps % 30 == 0 then
					popupPulses = popupPulses + 1
					local ok, err = nsmbu.Input.press("A", 3)
					note(step, string.format("press A file_select phase=%s", phaseText()))
					if ok == nil and err ~= nil and not pressErrLogged then
						pressErrLogged = true
						note(step, err)
					end
				end
			else
				local ok, err = nsmbu.Scene.open_title()
				if ok == true then
					note(step, "open_title")
				else
					note(step, string.format("open_title %s", err or "refused"))
				end
			end
		end
	end
	if pendingApply and not opened then
		tryApplySave(step)
	end
	if pendingLoad and opened and not requested then
		tryLoadPack(step)
	end
end
nsmbu.Hooks.onUnload = function()
	done = false
	opened = false
	requested = false
	last = ""
	logicStep = 0
	pendingPopup = false
	pendingApply = false
	pendingLoad = false
	pendingCourseUp = false
	fileSelectSteps = 0
	popupPulses = 0
	pressErrLogged = false
end
return ____exports
