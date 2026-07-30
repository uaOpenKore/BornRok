--
-- AI v1.0.0325 - By Hattero
--

------------------------------
-- Attack
------------------------------
-- ATTACK_ASSIST_PARTY_MEMBERS	= 1	-- [doesn't work] whether to attack monsters that are attacking party members or their homunculi
ATTACK_MIN_HP_PERCENT		= 25	-- only attack passive monsters if hp percent is at least this amount

NO_ATTACK_LIST = {					-- never attack these monsters, even if attacked
	1078, -- Red plant
	1079, -- Blue plant
	1080, -- Green plant
	1081, -- Yellow plant
	1082, -- White plant
	1083, -- Shining plant
	1084, -- Black Mushroom
	1085, -- Red Mushroom
	
	1590, -- Geographer (Slave)
	1555, -- Parasite (Slave)
	1575, -- Flora (Slave)
	1579, -- Hydra (Slave)
	1589  -- Mandragora (Slave)
}

ATTACK_ON_SIGHT_LIST = {			-- only start attacking these monsters; Empty = attack all
	--[[
	-- Culvert
	1005, -- Familiar
	1054, -- Male Thief Bug
	1077, -- Poison Spore
	1111  -- Drainliar
	--]]
}

MON_IGNORE_LIST = {					-- do not start attacking these monsters; only used when ATTACK_ON_SIGHT_LIST is empty
	--[[
	1002  -- Poring
	--]]
	1048, -- Thief Bug Egg
	1051, -- Thief Bug
	1053  -- Female Thief Bug
}
------------------------------


------------------------------
-- Skills
------------------------------
-- Amistr
BULWARK_LEVEL				= 3
BULWARK_MIN_SP_PERCENT		= 90
BULWARK_MAX_HP_PERCENT		= 95

-- Filir
MOONLIGHT_MIN_SP_PERCENT	= 90

FLITTING_LEVEL				= 3
FLITTING_MIN_SP_PERCENT		= 85
FLITTING_MAX_HP_PERCENT		= 99

OVERSPEED_LEVEL				= 0
OVERSPEED_MIN_SP_PERCENT	= 88
OVERSPEED_MAX_HP_PERCENT	= 99

-- Lif

-- Vanilmirth
CAPRICE_MIN_SP_PERCENT		= 90
------------------------------


------------------------------
-- Auto Aid Potion
------------------------------
MIN_HP_PERCENT1		= 25
POT_LEVEL1			= 1
MIN_HP_PERCENT2		= 10
POT_LEVEL2			= 0
MIN_HP_PERCENT3		= 5
POT_LEVEL3			= 0
------------------------------

