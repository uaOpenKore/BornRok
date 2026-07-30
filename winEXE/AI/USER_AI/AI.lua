--
-- AI v1.0.0325 - By Hattero
--

require "./AI/USER_AI/Const.lua"
require "./AI/USER_AI/Util.lua"					
require "./AI/USER_AI/Options.lua"
require "./AI/USER_AI/MyFunc.lua"

-----------------------------
-- state
-----------------------------
IDLE_ST					= 0
FOLLOW_ST				= 1
CHASE_ST				= 2
ATTACK_ST				= 3
MOVE_CMD_ST				= 4
STOP_CMD_ST				= 5
ATTACK_OBJECT_CMD_ST	= 6
ATTACK_AREA_CMD_ST		= 7
PATROL_CMD_ST			= 8
HOLD_CMD_ST				= 9
SKILL_OBJECT_CMD_ST		= 10
SKILL_AREA_CMD_ST		= 11
FOLLOW_CMD_ST			= 12
----------------------------

------------------------------------------
-- global variable
------------------------------------------
MyState				= IDLE_ST	-- 최초의 상태는 휴식
MyEnemy				= 0		-- 적 id
MyDestX				= 0		-- 목적지 x
MyDestY				= 0		-- 목적지 y
MyPatrolX			= 0		-- 정찰 목적지 x
MyPatrolY			= 0		-- 정찰 목적지 y
ResCmdList			= List.new()	-- 예약 명령어 리스트 
MyID				= 0		-- 호문클루스 id
MySkill				= 0		-- 호문클루스의 스킬
MySkillLevel		= 0		-- 호문클루스의 스킬 레벨

MyTargetX			= 0
MyTargetY			= 0

RandomMoveTick		= 0

MyOwner				= 0
FollowState			= 1
FollowTarget		= 0
Aggressive			= 0 -- find passive target
ForceAttack			= 0 -- attack even if the target attacks other people
------------------------------------------

------------- command process  ---------------------

function	OnMOVE_CMD (x,y)	
	TraceAI ("OnMOVE_CMD")

	if ( x == MyDestX and y == MyDestY and MOTION_MOVE == GetV(V_MOTION,MyID)) then
		return			-- already moving there
	end

	MoveTo(x,y)
	MyState = MOVE_CMD_ST
	MyEnemy = 0
	MySkill = 0
	ForceAttack = 0
	SetFollowTargetAt(x,y)
end

function	OnSTOP_CMD ()
	TraceAI ("OnSTOP_CMD")

	if (GetV(V_MOTION,MyID) ~= MOTION_STAND) then
		Move (MyID,GetV(V_POSITION,MyID))
	end
	MyState = IDLE_ST
	MyDestX = 0
	MyDestY = 0
	MyEnemy = 0
	MySkill = 0
	ForceAttack = 0
	-- FollowState = 1
end

function	OnATTACK_OBJECT_CMD (id)
	TraceAI ("OnATTACK_OBJECT_CMD")

	MySkill = 0
	MyEnemy = id
	MyState = CHASE_ST
	ForceAttack = 1  -- attack even if it is attacking other people
	TraceAI("Attack monster ID="..MyEnemy.." TYPE="..GetV(V_HOMUNTYPE,MyEnemy))
end

function	OnATTACK_AREA_CMD (x,y)
	TraceAI ("OnATTACK_AREA_CMD")

	if (x ~= MyDestX or y ~= MyDestY or MOTION_MOVE ~= GetV(V_MOTION,MyID)) then
		Move (MyID,x,y)	
	end
	MyDestX = x
	MyDestY = y
	MyEnemy = 0
	MyState = ATTACK_AREA_CMD_ST
	ForceAttack = 0
end

function	OnPATROL_CMD (x,y)
	TraceAI ("OnPATROL_CMD")

	MyPatrolX,MyPatrolY = GetV(V_POSITION,MyID)
	MyDestX = x
	MyDestY = y
	Move (MyID,x,y)
	MyState = PATROL_CMD_ST
	ForceAttack = 0
end

function	OnHOLD_CMD ()
	TraceAI ("OnHOLD_CMD")

	MyDestX = 0
	MyDestY = 0
	MyEnemy = 0
	MyState = HOLD_CMD_ST
	ForceAttack = 0
end

function	OnSKILL_OBJECT_CMD (level,skill,id)
	TraceAI ("OnSKILL_OBJECT_CMD")

	MySkillLevel = level
	MySkill = skill
	MyEnemy = id
	MyState = CHASE_ST
	ForceAttack = 1
end

function	OnSKILL_AREA_CMD (level,skill,x,y)
	TraceAI ("OnSKILL_AREA_CMD")

	Move (MyID,x,y)
	MyDestX = x
	MyDestY = y
	MySkillLevel = level
	MySkill = skill
	MyState = SKILL_AREA_CMD_ST
	ForceAttack = 0
end

function	OnFOLLOW_CMD ()
	-- Happen when Alt + T
	if (Aggressive == 0) then
		Aggressive = 1
	elseif (Aggressive == 1) then
		Aggressive = 0
		StopAction()
	end

	--[[
	-- 대기명령은 대기상태와 휴식상태를 서로 전환시킨다. 
	if (MyState ~= FOLLOW_CMD_ST) then
		MoveToOwner (MyID)
		MyState = FOLLOW_CMD_ST
		MyDestX, MyDestY = GetV (V_POSITION,GetV(V_OWNER,MyID))
		MyEnemy = 0 
		MySkill = 0
		TraceAI ("OnFOLLOW_CMD")
	else
		MyState = IDLE_ST
		MyEnemy = 0 
		MySkill = 0
		TraceAI ("FOLLOW_CMD_ST --> IDLE_ST")
	end
	--]]
end

function	ProcessCommand (msg)
	if		(msg[1] == MOVE_CMD) then
		OnMOVE_CMD (msg[2],msg[3])
		TraceAI ("MOVE_CMD")
	elseif	(msg[1] == STOP_CMD) then
		OnSTOP_CMD ()
		TraceAI ("STOP_CMD")
	elseif	(msg[1] == ATTACK_OBJECT_CMD) then
		OnATTACK_OBJECT_CMD (msg[2])
		TraceAI ("ATTACK_OBJECT_CMD")
	elseif	(msg[1] == ATTACK_AREA_CMD) then
		OnATTACK_AREA_CMD (msg[2],msg[3])
		TraceAI ("ATTACK_AREA_CMD")
	elseif	(msg[1] == PATROL_CMD) then
		OnPATROL_CMD (msg[2],msg[3])
		TraceAI ("PATROL_CMD")
	elseif	(msg[1] == HOLD_CMD) then
		OnHOLD_CMD ()
		TraceAI ("HOLD_CMD")
	elseif	(msg[1] == SKILL_OBJECT_CMD) then
		OnSKILL_OBJECT_CMD (msg[2],msg[3],msg[4],msg[5])
		TraceAI ("SKILL_OBJECT_CMD")
	elseif	(msg[1] == SKILL_AREA_CMD) then
		OnSKILL_AREA_CMD (msg[2],msg[3],msg[4],msg[5])
		TraceAI ("SKILL_AREA_CMD")
	elseif	(msg[1] == FOLLOW_CMD) then
		OnFOLLOW_CMD ()
		TraceAI ("FOLLOW_CMD")
	end
end

-------------- state process  --------------------

function	OnIDLE_ST ()	
	TraceAI ("OnIDLE_ST")

	local cmd = List.popleft(ResCmdList)
	if (cmd ~= nil) then		
		ProcessCommand (cmd)	-- 예약 명령어 처리 
		return 
	end

	local	object = GetEnemy()
	if (object ~= 0) then							-- ATTACKED_IN
		MyState = CHASE_ST
		MyEnemy = object
		TraceAI ("IDLE_ST -> CHASE_ST : ATTACKED_IN")
		OnCHASE_ST() -- start chasing
		return
	end

	if (MoveStayOnScreen()) then
		return
	end
	
	-- stay near follow target
	if (FollowState ~= 0) then
		local distance = GetDistanceFromTarget()
		if (distance ~= -1) then
			if (distance > 3) then		-- TARGET_OUTSIGHT_IN
				MyState = FOLLOW_ST
				TraceAI ("IDLE_ST -> FOLLOW_ST")
				OnFOLLOW_ST() -- start following
				return;
			end

			-- If the follow target moves, then move too.
			if (MoveAheadTarget()) then
				return
			end
			
			-- Otherwise, if full hp/sp, move randomly
			if (GetV (V_HP,MyID) >= GetV (V_MAXHP,MyID) and GetV (V_SP,MyID) >= GetV (V_MAXSP,MyID)) then
				local tick = GetTick()
				if (tick >= RandomMoveTick) then
					local x,y = GetFollowTargetPos()
					x = x + math.random(-2,2)
					y = y + math.random(-2,2)
					RandomMoveTick = tick + 5000
					MoveToward(x,y)
				end
			end
		end
	end
end

function	OnFOLLOW_ST ()
	TraceAI ("OnFOLLOW_ST")

	if (MoveStayOnScreen()) then
		return
	end
	
	local x, y = GetFollowTargetPos()
	if (x == -1) then  -- target is off screen
		return
	end
	local dx = x - MyTargetX
	local dy = y - MyTargetY
	if (dx > 0) then
		x = x + 2
	elseif (dx < 0) then
		x = x - 2
	end
	if (dy > 0) then
		y = y + 2
	elseif (dy < 0) then
		y = y - 2
	end

	if (dx == 0 and dy == 0 and GetDistanceFromTarget() <= 3) then		--  DESTINATION_ARRIVED_IN 
		MyState = IDLE_ST
		TraceAI ("FOLLOW_ST -> IDLE_ST")
		return;
	elseif (MyDestX ~= x or MyDestY ~= y or GetV(V_MOTION,MyID) == MOTION_STAND) then
		MoveToward(x,y)
		TraceAI ("FOLLOW_ST -> FOLLOW_ST")
		return;
	end
end

function	OnCHASE_ST ()
	TraceAI ("OnCHASE_ST")

	if (true == IsOutOfSight(MyID,MyEnemy)) then	-- ENEMY_OUTSIGHT_IN
		MyState = IDLE_ST
		MyEnemy = 0
		MyDestX, MyDestY = 0,0
		TraceAI ("CHASE_ST -> IDLE_ST : ENEMY_OUTSIGHT_IN")
		return
	end
	
	if (ForceAttack == 0 and IsKS()) then -- do not KS
		StopAction()
		return
	end
	
	if (true == IsInAttackSight(MyID,MyEnemy)) then  -- ENEMY_INATTACKSIGHT_IN
		MyState = ATTACK_ST
		TraceAI ("CHASE_ST -> ATTACK_ST : ENEMY_INATTACKSIGHT_IN")
		OnATTACK_ST() -- attack now
		return
	end

	local x, y = GetV(V_POSITION,MyEnemy)
	if (MyDestX ~= x or MyDestY ~= y) then			-- DESTCHANGED_IN
		MoveToward(x,y)
		TraceAI ("CHASE_ST -> CHASE_ST : DESTCHANGED_IN")
		return
	end
end

function	OnATTACK_ST ()
	TraceAI ("OnATTACK_ST")
	
	if (true == IsOutOfSight(MyID,MyEnemy)) then	-- ENEMY_OUTSIGHT_IN
		ForceAttack = 0
		MyState = IDLE_ST
		TraceAI ("ATTACK_ST -> IDLE_ST")
		return
	end

	if (MOTION_DEAD == GetV(V_MOTION,MyEnemy)) then   -- ENEMY_DEAD_IN
		ForceAttack = 0
		MyState = IDLE_ST
		TraceAI ("ATTACK_ST -> IDLE_ST")
		return
	end
		
	if (ForceAttack == 0 and IsKS()) then -- do not KS
		StopAction()
		return
	end
	
	if (false == IsInAttackSight(MyID,MyEnemy)) then  -- ENEMY_OUTATTACKSIGHT_IN
		MyState = CHASE_ST
		MyDestX, MyDestY = GetV (V_POSITION,MyEnemy);
		MoveToward(MyDestX,MyDestY)
		TraceAI ("ATTACK_ST -> CHASE_ST  : ENEMY_OUTATTACKSIGHT_IN")
		return
	end
	
	if (MySkill == 0) then
		AutoCastSupportSkills(MyID)
		AutoCastAttackSkills(MyID,MyEnemy)
		Attack (MyID,MyEnemy)
	else
		SkillObject (MyID,MySkillLevel,MySkill,MyEnemy)
		MySkill = 0
	end
	TraceAI ("ATTACK_ST -> ATTACK_ST  : ENERGY_RECHARGED_IN")
	return
end

function	OnMOVE_CMD_ST ()
	TraceAI ("OnMOVE_CMD_ST")

	local x, y = GetV (V_POSITION,MyID)
	if (x == MyDestX and y == MyDestY) then				-- DESTINATION_ARRIVED_IN
		MyState = IDLE_ST
	end
end

function OnSTOP_CMD_ST ()

end

function OnATTACK_OBJECT_CMD_ST ()

end

function OnATTACK_AREA_CMD_ST ()
	TraceAI ("OnATTACK_AREA_CMD_ST")

	local	object = GetEnemy (MyID)

	if (object ~= 0) then							-- MYOWNER_ATTACKED_IN or ATTACKED_IN
		MyState = CHASE_ST
		MyEnemy = object
		return
	end

	local x , y = GetV (V_POSITION,MyID)
	if (x == MyDestX and y == MyDestY) then			-- DESTARRIVED_IN
			MyState = IDLE_ST
	end
end

function OnPATROL_CMD_ST ()
	TraceAI ("OnPATROL_CMD_ST")

	local	object = GetEnemy (MyID)

	if (object ~= 0) then							-- MYOWNER_ATTACKED_IN or ATTACKED_IN
		MyState = CHASE_ST
		MyEnemy = object
		TraceAI ("PATROL_CMD_ST -> CHASE_ST : ATTACKED_IN")
		return
	end

	local x , y = GetV (V_POSITION,MyID)
	if (x == MyDestX and y == MyDestY) then			-- DESTARRIVED_IN
		MyDestX = MyPatrolX
		MyDestY = MyPatrolY
		MyPatrolX = x
		MyPatrolY = y
		Move (MyID,MyDestX,MyDestY)
	end
end

function OnHOLD_CMD_ST ()
	TraceAI ("OnHOLD_CMD_ST")
	
	if (MyEnemy ~= 0) then
		local d = GetDistance(MyEnemy,MyID)
		if (d ~= -1 and d <= GetV(V_ATTACKRANGE,MyID)) then
				Attack (MyID,MyEnemy)
		else
			MyEnemy = 0;
		end
		return
	end

	local	object = GetEnemy (MyID)
	if (object == 0) then							
		return
	end

	MyEnemy = object
end

function OnSKILL_OBJECT_CMD_ST ()
	
end

function OnSKILL_AREA_CMD_ST ()
	TraceAI ("OnSKILL_AREA_CMD_ST")

	local x , y = GetV (V_POSITION,MyID)
	if (GetDistance(x,y,MyDestX,MyDestY) <= GetV(V_SKILLATTACKRANGE,MyID,MySkill)) then	-- DESTARRIVED_IN
		SkillGround (MyID,MySkillLevel,MySkill,MyDestX,MyDestY)
		MyState = IDLE_ST
		MySkill = 0
	end
end

function OnFOLLOW_CMD_ST ()
	TraceAI ("OnFOLLOW_CMD_ST")

	local ownerX, ownerY, myX, myY
	ownerX, ownerY = GetV (V_POSITION,GetV(V_OWNER,MyID)) -- 주인
	myX, myY = GetV (V_POSITION,MyID)					  -- 나 
	
	local d = GetDistance (ownerX,ownerY,myX,myY)

	if ( d <= 3) then									  -- 3셀 이하 거리면 
		return 
	end

	local motion = GetV (V_MOTION,MyID)
	if (motion == MOTION_MOVE) then                       -- 이동중
		d = GetDistance (ownerX, ownerY, MyDestX, MyDestY)
		if ( d > 3) then                                  -- 목적지 변경 ?
			MoveToOwner (MyID)
			MyDestX = ownerX
			MyDestY = ownerY
			return
		end
	else                                                  -- 다른 동작 
		MoveToOwner (MyID)
		MyDestX = ownerX
		MyDestY = ownerY
		return
	end
	
end

------------------------------------
-- Helper Functions
------------------------------------

function AutoAidPotion()
	local curhp = GetHPPercent(MyID)
	local potlevel = 0
	if (POT_LEVEL3 ~= 0 and curhp <= MIN_HP_PERCENT3) then
		potlevel = POT_LEVEL3
	elseif (POT_LEVEL2 ~= 0 and curhp <= MIN_HP_PERCENT2) then
		potlevel = POT_LEVEL2
	elseif (POT_LEVEL1 ~= 0 and curhp <= MIN_HP_PERCENT1) then
		potlevel = POT_LEVEL1
	end
	if (potlevel ~= 0) then
		SkillObject(MyID,potlevel,231,MyID)
	end
end

function MoveToward(x,y)	-- move toward the position
	local curX, curY = GetV(V_POSITION,MyID)
	while (GetTriDistance(x,y,curX,curY) > 15) do
		x = math.floor((x+curX)/2)
		y = math.floor((y+curY)/2)
	end
	x,y = MoveBorder(MyOwner,x,y)
	Move(MyID,x,y)
	MyDestX = x
	MyDestY = y
	return x,y
end

function MoveTo(x,y)		-- move to the position
	local curX, curY = GetV(V_POSITION,MyID)
	if (GetTriDistance(x,y,curX,curY) > 15) then	-- if too far,
		List.pushleft(ResCmdList,{MOVE_CMD,x,y})	-- try again later
	end
	MoveToward(x,y)
end

function MoveAheadTarget()
	local x,y = GetFollowTargetPos()
	local dx = x - MyTargetX
	local dy = y - MyTargetY
	if (dx > 0) then
		x = x + 2
	elseif (dx < 0) then
		x = x - 2
	end
	if (dy > 0) then
		y = y + 2
	elseif (dy < 0) then
		y = y - 2
	end

	if (dx ~= 0 or dy ~= 0) then
		MoveToward(x,y)
		return true
	else
		return false
	end
end

function MoveStayOnScreen()
	local x, y = GetV(V_POSITION,MyID)
	local tx,ty = MoveBorder(MyOwner,x,y)
	if (tx~=x or ty~=y) then
		MoveToward(tx,ty)
		return true
	else
		return false
	end
end

function GetFollowTargetPos()
	if (FollowState == 1) then
		return GetV(V_POSITION,MyOwner)
	elseif (FollowState == 2 and FollowTarget ~= 0) then
		return GetV(V_POSITION,FollowTarget)
	else
		return GetV(V_POSITION,MyOwner)
	end
end

function GetDistanceFromTarget()
	if (FollowState == 1) then
		return GetAbsDistance2(MyID, MyOwner)
	elseif (FollowState == 2 and FollowTarget ~= 0) then
		return GetAbsDistance2(MyID, FollowTarget)
	else
		return GetAbsDistance2(MyID, MyOwner)
	end
end

function SetFollowTargetAt(x,y)
	FollowState = 0
	FollowTarget = 0
	local actors = GetActors()
	for i,v in ipairs(actors) do
		if (v ~= MyID and IsMonster(v) ~= 1) then
			local tx,ty = GetV(V_POSITION,v)
			if (tx == x and ty == y) then
				-- TraceAI("Set to follow: "..v)
				-- local towner = GetV(V_OWNER,v)
				-- TraceAI("Follow ["..v..","..GetV(V_HOMUNTYPE,v)..","..GetV(V_HP,v)..","..GetV(V_MAXHP,v)..";"..towner..","..GetV(V_HOMUNTYPE,towner)..","..GetV(V_HP,towner)..","..GetV(V_MAXHP,towner).."].")
				if (v == MyOwner) then
					FollowState = 1
					FollowTarget = 0
				else
					FollowState = 2
					FollowTarget = v
				end
			end
		end
	end
end

function IsAlly(id)
	return (id == MyID or id == MyOwner)
end

function IsKS()
	local target = GetV(V_TARGET,MyEnemy)
	return (target ~= 0 and IsAlly(target) ~= true)
end

function StopAction()
	if (GetV(V_MOTION,MyID) ~= MOTION_STAND) then
		Move (MyID,GetV(V_POSITION,MyID))
	end
	MyState = IDLE_ST
	MyEnemy = 0
	MySkill = 0
	MyDestX,MyDestY = 0,0
	ForceAttack = 0
end

function IsNoAttack(id)
	local type = GetV(V_HOMUNTYPE,id)
	return IsInList(NO_ATTACK_LIST,type)	
end

function IsAttackOnSight(id)
	local type = GetV(V_HOMUNTYPE,id)
	if (ATTACK_ON_SIGHT_LIST[1] == nil) then
		-- attack all monsters except on MON_IGNORE_LIST
		return (IsMonster(id) == 1 and IsInList(MON_IGNORE_LIST,type) ~= true)
	else
		return IsInList(ATTACK_ON_SIGHT_LIST,type)
	end
end

function GetEnemy()
	local otarget = GetV(V_TARGET,MyOwner)
	local aenemys = {}
	local aindex = 1
	local penemys = {}
	local pindex = 1
	local tlist = {}
	local tindex = 1
	local actors = GetActors ()
	for i,v in ipairs(actors) do
		-- attack owner's target first
		if (v == otarget and IsMonster(v) == 1) then
			return v
		end
		
		if (v ~= MyOwner and v ~= MyID and IsNoAttack(v) ~= true) then
			local target = GetV(V_TARGET,v)
			if (target == 0) then
				if (Aggressive == 1 and IsAttackOnSight(v)) then
					penemys[pindex] = v
					pindex = pindex+1
				end
			elseif (IsAlly(target)) then
				if (IsMonster(v) == 1) then
					aenemys[aindex] = v
					aindex = aindex+1
				else
					--[[
					local motion = GetV(V_MOTION,v)
					if (motion == MOTION_ATTACK or motion == MOTION_ATTACK2) then
						aenemys[aindex] = v
						aindex = aindex+1
					end
					--]]
				end
			else
				tlist[tindex] = target
				tindex = tindex+1
			end
		end
	end
	
	-- pick current enemies before attacking new ones
	local curhp = GetHPPercent(MyID)
	local enemys
	if (aindex > 1) then
		enemys = aenemys
	elseif (curhp >= ATTACK_MIN_HP_PERCENT) then
		enemys = {}
		local index = 1
		for i,v in ipairs(penemys) do
			if (IsInList(tlist,v) ~= true) then
				enemys[index] = v
				index = index+1
			end
		end
	else
		return 0
	end

	local result = 0
	local min_dis = 100
	for i,v in ipairs(enemys) do
		local dis = GetDistance2 (MyID,v)
		if (dis < min_dis) then
			result = v
			min_dis = dis
		end
	end
	if (result ~=0 and GetAbsDistance2(MyOwner,result) < 10) then
		return result
	else
		return 0
	end
end

------------------------------------

function AI(myid)
	MyID = myid
	MyOwner = GetV(V_OWNER,myid)
	AutoAidPotion()
	
	local msg	= GetMsg (myid)			-- command
	local rmsg	= GetResMsg (myid)		-- reserved command

	if msg[1] == NONE_CMD then
		if rmsg[1] ~= NONE_CMD then
			if List.size(ResCmdList) < 10 then
				List.pushright (ResCmdList,rmsg) -- 예약 명령 저장
			end
		end
	else
		List.clear (ResCmdList)	-- 새로운 명령이 입력되면 예약 명령들은 삭제한다.  
		ProcessCommand (msg)	-- 명령어 처리 
	end

	-- 상태 처리 
 	if (MyState == IDLE_ST) then
		OnIDLE_ST ()
	elseif (MyState == CHASE_ST) then					
		OnCHASE_ST ()
	elseif (MyState == ATTACK_ST) then
		OnATTACK_ST ()
	elseif (MyState == FOLLOW_ST) then
		OnFOLLOW_ST ()
	elseif (MyState == MOVE_CMD_ST) then
		OnMOVE_CMD_ST ()
	elseif (MyState == STOP_CMD_ST) then
		OnSTOP_CMD_ST ()
	elseif (MyState == ATTACK_OBJECT_CMD_ST) then
		OnATTACK_OBJECT_CMD_ST ()
	elseif (MyState == ATTACK_AREA_CMD_ST) then
		OnATTACK_AREA_CMD_ST ()
	elseif (MyState == PATROL_CMD_ST) then
		OnPATROL_CMD_ST ()
	elseif (MyState == HOLD_CMD_ST) then
		OnHOLD_CMD_ST ()
	elseif (MyState == SKILL_OBJECT_CMD_ST) then
		OnSKILL_OBJECT_CMD_ST ()
	elseif (MyState == SKILL_AREA_CMD_ST) then
		OnSKILL_AREA_CMD_ST ()
	elseif (MyState == FOLLOW_CMD_ST) then
		OnFOLLOW_CMD_ST ()
	end

	MyTargetX, MyTargetY = GetFollowTargetPos()
end
