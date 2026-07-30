--
-- AI v1.0.0325 - By Hattero
--

require "./AI/USER_AI/Const.lua"

function MoveBorder(owner,x,y)  -- limit homunculus movement on screen
	local ownerX, ownerY = GetV(V_POSITION, owner)
	if (x < ownerX-10) then
		x = ownerX-10
	elseif (x > ownerX+10) then
		x = ownerX+10
	end
	if (y < ownerY-10) then
		y = ownerY-10
	elseif (y > ownerY+10) then
		y = ownerY+10
	end
	return x,y
end

function GetAbsDistance(x1,y1,x2,y2)
	local dx = math.abs(x2 - x1)
	local dy = math.abs(y2 - y1)
	if (dx > dy) then
		return dx
	else
		return dy
	end
end

function GetAbsDistance2(id1,id2)
	local x1, y1 = GetV(V_POSITION,id1)
	local x2, y2 = GetV(V_POSITION,id2)
	return GetAbsDistance(x1,y1,x2,y2)
end

function GetTriDistance(x1,y1,x2,y2)
	return (math.abs(x1-x2) + math.abs(y1-y2))
end

function IsInList(list,obj)
	for i,v in ipairs(list) do
		if (v == obj) then
			return true
		end
	end
	return false
end

--[[
-- doesn't work
function IsPartyMember(id)
	local hp = GetV(V_HP,id)
	return (hp ~= -1 and IsPlayer(id))
end
--]]

function IsNPC(id)
	local type = GetV(V_HOMUNTYPE,id)
	return ((type > 45 and type < 126) or (type > 699 and type < 1000))
end

function IsPlayer(id)
	local type = GetV(V_HOMUNTYPE,id)
	return (((type > 0 and type < 24) or (type > 4000 and type < 4046)) and id > 99999)
end

function IsHomunculus(id)
	local type = GetV(V_HOMUNTYPE,id)
	return ((type > 0 and type < 12) and id < 100000)
end

function GetHPPercent(myid)
	return 100*GetV(V_HP,myid)/GetV(V_MAXHP,myid)
end

function GetSPPercent(myid)
	return 100*GetV(V_SP,myid)/GetV(V_MAXSP,myid)
end


BulWarkDelayList	= { 40000,35000,30000,25000,20000 }
FlittingDelayList	= { 60000,70000,80000,90000,120000 }
OverspeedDelayList	= { 60000,70000,80000,90000,120000 }

CastDelay			= 0
BulwarkDelay		= 0
FlittingDelay		= 0
OverspeedDelay		= 0

function AutoCastSupportSkills(myid)
	local tick = GetTick()
	if (tick < CastDelay) then
		return
	end
	
	local type = GetV(V_HOMUNTYPE,myid)
	local cursp = GetSPPercent(myid)
	local curhp = GetHPPercent(myid)
	if (type == AMISTR or type == AMISTR_H) then
		-- Amistr Bulwark
		if (BULWARK_LEVEL > 0 and tick > BulwarkDelay and cursp >= BULWARK_MIN_SP_PERCENT and curhp <= BULWARK_MAX_HP_PERCENT) then
			SkillObject(myid,BULWARK_LEVEL,8006,myid)
			CastDelay = tick + 1000
			BulwarkDelay = tick + BulWarkDelayList[BULWARK_LEVEL]
			return
		end
	elseif (type == FILIR or type == FILIR_H) then
		-- Flitting
		if (FLITTING_LEVEL > 0 and tick > FlittingDelay and cursp >= FLITTING_MIN_SP_PERCENT and curhp <= FLITTING_MAX_HP_PERCENT) then
			SkillObject(myid,FLITTING_LEVEL,8010,myid)
			CastDelay = tick + 1000
			FlittingDelay = tick + FlittingDelayList[FLITTING_LEVEL]
			return
		end

		-- Overspeed
		if (OVERSPEED_LEVEL > 0 and tick > OverspeedDelay and cursp >= OVERSPEED_MIN_SP_PERCENT and curhp <= OVERSPEED_MAX_HP_PERCENT) then
			SkillObject(myid,OVERSPEED_LEVEL,8011,myid)
			CastDelay = tick + 1000
			OverspeedDelay = tick + OverspeedDelayList[ACC_FLIGHT_LEVEL]
			return
		end		
	elseif (type == VANILMIRTH or type == VANILMIRTH_H) then

	elseif (type == LIF or type == LIF_H) then

	end
end

function AutoCastAttackSkills(myid,myenemy)
	local type = GetV(V_HOMUNTYPE,myid)
	local cursp = GetSPPercent(myid)
	if (type == AMISTR or type == AMISTR_H) then

	elseif (type == FILIR or type == FILIR_H) then
		-- Moonlight
		if (cursp >= MOONLIGHT_MIN_SP_PERCENT) then
			SkillObject(myid,5,8009,myenemy)
			return
		end
	elseif (type == VANILMIRTH or type == VANILMIRTH_H) then
		-- Caprice
		if (cursp >= CAPRICE_MIN_SP_PERCENT) then
			SkillObject(myid,5,8013,myenemy)
			return
		end
	elseif (type == LIF or type == LIF_H) then

	end
end