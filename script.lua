cmd.echo('<script.lua>')

cfgfs.game_window_title_is('Team Fortress 2 - OpenGL')
cfgfs.init_after_cfg['comfig/modules_run.cfg'] = true
cfgfs.intercept_blackhole['comfig/echo.cfg'] = true

--------------------------------------------------------------------------------

-- crosshair and slot thing

do

class = global('class', 'scout')
slot = global('slot', 1)

-- mod = switch to that slot when holding shift
-- skip = pretend this slot doesn't exist and prevent scrolling to it
--        (slots >=4 can always be selected using number keys)
-- ctrl+alt+[slot] = toggle skip on the slot
-- ctrl+tab = switch between alternative crosshairs
xhs = global('xhs', {
	scout        = { mod=2,
	                 { {file=7, size=24}, -- shortstop
	                   {file=6} }, -- scattergun
	                 {file=5, size=22},
	                 {file=4} },

	soldier      = { mod=3,
	                 {file=6},
	                 {file=7, size=24},
	                 {file=4} },

	pyro         = { mod=3,
	                 {file=5, size=28},
	                 {file=7, size=24},
	                 {file=4} },

	demoman      = { mod=1,
	                 {file=2},
	                 {file=7, size=24},
	                 {file=4} },

	heavyweapons = { {file=3},
	                 {file=7, size=24},
	                 {file=4} },

	-- build pda switches to melee on build
	-- destruct pda remembers active slot
	engineer     = { { {file=6},
	                   {file=7, size=24} }, -- rescue ranger
	                 {file=5, size=22},
	                 {file=4},
	                 {skip=1, file=4},
	                 {skip=1} },

	medic        = { {file=5, size=24},
	                 {file=7, size=24},
	                 {file=4} },

	sniper       = { {file=5, size=20},
	                 {file=7, size=22},
	                 {file=4} },

	-- disguise remembers active slot
	spy          = { {file=5, size=20},
	                 {file=7, size=24},
	                 {file=4},
	                 {skip=1} },

	_v = 1,
})
-- fix up slots that don't have alt. crosshairs to have a list of one of them
for class, slots in pairs(xhs) do
	if class == '_v' then goto next end
	for n, slot in ipairs(slots) do
		if not slot[1] then
			slots[n] = {slot}
			for _, k in ipairs({'skip'}) do
				if slot[k] then
					slots[n][k] = slot[k]
					slot[k] = nil
				end
			end
		end
	end
	::next::
end

local mod_prev = nil

local xh_update = function (slot)
	local t = xhs[class][slot][1]
	if not t then return end
	if t.file then
		cvar.cl_crosshair_file = 'crosshair'..t.file
		cvar.cl_crosshair_scale = t.size or 32
	end
end
add_listener('slotchange', xh_update)

add_listener('+tab', function ()
	if is_pressed['ctrl'] then
		if #xhs[class][slot] > 1 then
			table.insert(xhs[class][slot], xhs[class][slot][1])
			table.remove(xhs[class][slot], 1)
			return xh_update(slot)
		else
			return cfg('slot6')
		end
	end
end)

local do_slot = function (n, is_mod, noskip)
	if not (n and xhs[class][n] and (noskip or not xhs[class][n].skip)) then
		return cfg('slot6')
	end
	if not is_mod then
		slot = n
	end
	mod_prev = nil
	cfgf('slot%d', n)
	fire_event('slotchange', n)
end

local getslot = function (slot, add)
	slot = (slot or #xhs[class])
	local slmax = #xhs[class]
	for i = 1, slmax-1 do
		local n = ((slot-1+add*i)%slmax)+1
		if not xhs[class][n].skip then
			return n
		end
	end
	return nil
end
slotcmd = function (n)
	return function ()
		if is_pressed['ctrl'] and is_pressed['alt'] then
			xhs[class][n].skip = (not xhs[class][n].skip) or nil
			if n == slot and xhs[class][n].skip then
				return do_slot(getslot(nil, 1))
			end
		end
		return do_slot(n, false, (n >= 4))
	end
end
invnext = function ()
	return do_slot(getslot(slot, 1))
end
invprev = function ()
	return do_slot(getslot(slot, -1))
end

mod_dn = function ()
	local mod = xhs[class].mod
	if not mod or mod == slot then return cfg('slot6') end
	do_slot(mod, true)
	mod_prev = slot
end
mod_up = function ()
	if mod_prev then
		do_slot(mod_prev)
	end
end

add_listener('classchange', function (cls)
	class = cls
	return do_slot(1)
end)

end

--------------------------------------------------------------------------------

-- crosshair colors

do

local xh_update = function ()
	local r, g, b = 0xff, 0x0, 0xff
	if is_pressed['mouse1'] then
		r, g, b = r~0xff, g~0xff, b~0xff
	end
	if is_pressed['mouse2'] then
		r = r~0xff
	end
	if is_pressed['mouse3'] then
		g, b = g~0xff, b~0xff
	end
	--[[
	r, g, b = math.floor(r*brightness),
	          math.floor(g*brightness),
	          math.floor(b*brightness)
	]]
	cvar.cl_crosshair_red   = r
	cvar.cl_crosshair_green = g
	cvar.cl_crosshair_blue  = b
end

attack1_dn = function () cmd('+attack') xh_update() cmd('spec_next') end
attack1_up = function () cmd('-attack') xh_update() end
attack2_dn = function () cmd('+attack2') xh_update() cmd('spec_prev') end
attack2_up = function () cmd('-attack2') xh_update() end
attack3_dn = function () cmd('+attack3') xh_update() end
attack3_up = function () cmd('-attack3') xh_update() end

add_listener('classchange', xh_update)

end

--------------------------------------------------------------------------------

nullcancel_pair = function (this_key, other_key, this_act, other_act)
	local this_dn, this_up = '+'..this_act, '-'..this_act
	local other_dn, other_up = '+'..other_act, '-'..other_act
	local this_dn_other_up = other_up..';'..this_dn
	local this_up_other_dn = this_up..';'..other_dn
	local dn = function ()
		if is_pressed[other_key]
		then return cfg(this_dn_other_up)
		else return cfg(this_dn)
		end
	end
	local up = function ()
		if is_pressed[other_key]
		then return cfg(this_up_other_dn)
		else return cfg(this_up)
		end
	end
	return dn, up
end

--------------------------------------------------------------------------------

-- this suck

local jump_dn = '+jump;slot6;spec_mode'
local jump_up = '-jump'

if 1 + 1 == 2 then
	local jump_ms = 700
	local hold_ms = 350

	local cmd_dn = jump_dn
	local cmd_up = jump_up

	jump_dn = function (_, key)
		local up_event_name = ('-'..key)
		local got_up_event = false
		repeat
			cfg(cmd_dn)
			got_up_event = wait_for_event(up_event_name, hold_ms)
			cfg(cmd_up)
			if got_up_event then break end
			got_up_event = wait_for_event(up_event_name, jump_ms-hold_ms)
		until got_up_event
	end
	jump_up = ''
end

--------------------------------------------------------------------------------

-- aliases

cmd.fixme = function ()
	cmd.record('\\')
	cmd.snd_restart()
	cmd.hud_reloadscheme()
	cmd.stop()
end

cmd.qqq = 'quit'

local wrap = function (sh)
	return function ()
		for line in io.popen(sh):lines() do
			cmd.echo(line)
		end
	end
end
cmd.fortune = wrap('fortune | cowsay')
cmd.top = wrap('top -bn1')

cmd.cfgfs_init = 'exec cfgfs/init'

cmd.release_all_keys = release_all_keys

cmd.disconnect2 = function ()
	for i = 1, 41 do
		cmd.cmd('cmd')
	end
end

add_listener('+mouse2', function ()
	if (class == 'engineer' or class == 'spy') and slot >= 4 then
		cmd.pda_click()
	end
end)

-- tell cfgfs you're a specific class (for testing)
local key2cls = {
	kp_end = 'scout',
	kp_downarrow = 'soldier',
	kp_pgdn = 'pyro',
	kp_leftarrow = 'demoman',
	kp_5 = 'heavyweapons',
	kp_rightarrow = 'engineer',
	kp_home = 'medic',
	kp_uparrow = 'sniper',
	kp_pgup = 'spy',
}
for k, v in pairs(key2cls) do
	add_listener('+'..k, function ()
		if is_pressed['alt'] then
			class = v
			fire_event('classchange', v)
		end
	end)
end

add_listener({'+1', '+2', '+3'}, function (_, key)
	cmd.next_map_vote(tonumber(key)-1)
end)

--------------------------------------------------------------------------------

-- huntsman charge bell
-- start + 1000ms = charged
-- start + 5000ms = wobbly
-- i think

local on_mouse1_dn = function ()
	if slot ~= 1 then return end

	if wait_for_event({'-mouse1', '+mouse2', 'slotchange'}, 1000) then
		return
	end
	cmd.slot6()

	if wait_for_event({'-mouse1', '+mouse2', 'slotchange'}, 4000) then
		return
	end
	cmd.slot6()
	wait(250)
	cmd.slot6()
end

add_listener('classchange', function (class)
	if class == 'sniper' then
		add_listener('+mouse1', on_mouse1_dn)
	else
		remove_listener('+mouse1', on_mouse1_dn)
	end
end)

--------------------------------------------------------------------------------

-- cvars

cvar.con_logfile = 'console.log'

cvar.sensitivity = 2.6

cvar.snd_musicvolume = 0
cvar.voice_scale = 0.5
cvar.volume = 1
cvar.cl_autoreload = 1
cvar.cl_customsounds = 1
cvar.cl_disablehtmlmotd = 1
cvar.cl_downloadfilter = 'nosounds' -- (all, none, nosounds, mapsonly)
cvar.cl_hud_playerclass_use_playermodel = 1
cvar.cl_mvm_wave_status_visible_during_wave = 1
cvar.cl_rumblescale = 0
cvar.con_enable = 0
cvar.engine_no_focus_sleep = 100
cvar.fov_desired = 90
do local refresh_rate = 60/1.001
   cvar.fps_max = refresh_rate*2+1 end
cvar.glow_outline_effect_enable = 1
cvar.hud_classautokill = 0
cvar.hud_combattext = 1
cvar.hud_combattext_doesnt_block_overhead_text = 1
cvar.hud_fastswitch = 1
cvar.hud_freezecamhide = 1
cvar.hud_medicautocallers = 1
cvar.hud_medicautocallersthreshold = 99
cvar.hud_medichealtargetmarker = 1
cvar.m_rawinput = 1
cvar.snd_mute_losefocus = 0
cvar.sv_allow_point_servercommand = 'always'
cvar.tf_backpack_page_button_delay = 0
cvar.tf_dingaling_volume = 1
cvar.tf_dingalingaling = 1
cvar.tf_hud_target_id_disable_floating_health = 1
cvar.tf_remember_activeweapon = 1
cvar.tf_respawn_on_loadoutchanges = 0
cvar.tf_scoreboard_mouse_mode = 2
cvar.tf_scoreboard_ping_as_text = 1
cvar.tf_sniper_fullcharge_bell = 1
cvar.viewmodel_fov = 75
cvar.voice_overdrive = 1

cvar.mat_disable_lightwarp = 0
cvar.r_lightaverage = 0
cvar.r_rimlight = 1

add_listener({'classchange', 'slotchange'}, function ()
	local use_world_model = 1
	if class == 'sniper' and slot == 1 then
		use_world_model = 0 -- huntsman
	end
	if class == 'spy' then
		use_world_model = 0 -- cloak
	end
	cvar.cl_first_person_uses_world_model = use_world_model
end)

-- sto = 20
-- lux = 47
cvar.tf_mm_custom_ping_enabled = 1
cvar.tf_mm_custom_ping = 35

--------------------------------------------------------------------------------

-- binds

-- https://wiki.teamfortress.com/wiki/List_of_default_keys

-- things that need the key bound directly:
-- * +reload for switching disguise team
-- * say, say_team for using the chat
-- * +taunt to use the normal taunt from the menu
-- * lastinv to cancel the taunt menu

cmd.unbindall()

bind('mouse1',                          attack1_dn, attack1_up)
bind('mouse2',                          attack2_dn, attack2_up)
bind('mouse3',                          attack3_dn, attack3_up)
bind('mouse4',                          'voicemenu 0 7') -- y*s
bind('mouse5',                          'voicemenu 0 6') -- n*
bind('mwheeldown',                      invnext)
bind('mwheelup',                        invprev)

bind('escape',                          'cancelselect')
bind('f1',                              'incrementvar net_graph 0 6 6')
bind('f2',                              'screenshot')
bind('f3',                              '')
bind('f4',                              'player_ready_toggle')
bind('f5',                              '')
bind('f6',                              '')
bind('f7',                              '')
cmd.bind('f8',                          'exec cfgfs/click')
cmd.bind('f9',                          'exec cfgfs/click')
bind('f10', function ()
	local old = cvar.con_enable
	cvar.con_enable = 1
	cmd.toggleconsole()
	cvar.con_enable = old
end)
cmd.bind('f11',                         'exec cfgfs/click')
bind('f12',                             '')

bind('scrolllock',                      '')
bind('pause',                           '')

bind('\\',                              '') -- valve homos broke this key and now it can't be bound
bind('1',                               slotcmd(1))
bind('2',                               slotcmd(2))
bind('3',                               slotcmd(3))
bind('4',                               slotcmd(4))
bind('5',                               slotcmd(5))
bind('6',                               slotcmd(6))
bind('7',                               slotcmd(7))
bind('8',                               slotcmd(8))
bind('9',                               slotcmd(9))
bind('0',                               slotcmd(10))
bind('=',                               '')
bind('[',                               '')
bind('backspace',                       '')

bind('ins',                             '')
bind('home',                            '')
bind('pgup',                            '')

bind('tab',                             '+showscores;cl_showfps 2;cl_showpos 1',
                                        '-showscores;cl_showfps 0;cl_showpos 0')
bind('q', function ()
	if is_pressed['alt'] then
		cmd.bots()
	else
		cmd.kptadpb()
	end
end)
bind('w',                               nullcancel_pair('w', 's', 'forward', 'back'))
bind('e',                               'voicemenu 0 0')
cmd.bind('r',                           '+reload')
bind('t',                               '+use_action_slot_item')
cmd.bind('y',                           'say')
cmd.bind('u',                           'say_team')
bind('i',                               'callvote')
bind('o',                               'kill')
bind('p',                               'explode')
bind(']',                               '')
bind('semicolon',                       '')
bind('enter',                           '')

bind('del',                             '')
bind('end',                             '')
bind('pgdn',                            '')

bind('capslock',                        '')
bind('a',                               nullcancel_pair('a', 'd', 'moveleft', 'moveright'))
bind('s',                               nullcancel_pair('s', 'w', 'back', 'forward'))
bind('d',                               nullcancel_pair('d', 'a', 'moveright', 'moveleft'))
bind('f',                               '+inspect')
bind('g', function (_, key)
	repeat
		cmd.cmd('taunt')
	until wait_for_event('-'..key, 100/3)
end)
cmd.bind('h',                           'lastinv')
bind('j',                               'cl_trigger_first_notification')
bind('k',                               'cl_decline_first_notification')
bind('l', function (_, key)
	repeat
		cmd.cmd('dropitem')
	until wait_for_event('-'..key, 100/3)
end)
bind('semicolon',                       '')
bind("'",                               '')
bind('/',                               '')

bind('shift',                           mod_dn, mod_up)
bind('z',                               'voice_menu_1')
bind('x',                               'voice_menu_2')
bind('c',                               'voice_menu_3')
bind('v',                               'noclip')
bind('b',                               'lastdisguise')
bind('n',                               'open_charinfo_backpack')
bind('m',                               'open_charinfo_direct')
bind(',',                               'changeclass')
bind('.', function ()
	if is_pressed['alt'] then
		cmd.autoteam()
	else
		cmd.changeteam()
	end
end)
bind('-',                               '')
bind('rshift',                          '')

bind('ctrl',                            '+duck')
bind('lwin',                            '')
bind('alt',                             '+strafe')
bind('space',                           jump_dn, jump_up)
bind('ralt',                            '')
bind('rwin',                            '')
bind('app',                             '')
bind('rctrl',                           '')

bind('uparrow',                         '')
bind('downarrow',                       '')
bind('leftarrow',                       '')
bind('rightarrow',                      '')

bind('numlock',                         '')
bind('kp_slash',                        '')
bind('kp_multiply',                     '')
bind('kp_minus',                        '')

bind('kp_home',                         '') -- 7
bind('kp_uparrow',                      '') -- 8
bind('kp_pgup',                         '') -- 9
bind('kp_plus',                         '')

bind('kp_leftarrow',                    '') -- 4
bind('kp_5',                            '') -- 5
bind('kp_rightarrow',                   '') -- 6

bind('kp_end',                          '') -- 1
bind('kp_downarrow',                    '') -- 2
bind('kp_pgdn',                         '') -- 3
bind('kp_enter',                        '')

bind('kp_ins',                          '') -- 0
bind('kp_del',                          '') -- ,

--------------------------------------------------------------------------------

add_listener('attention', function (active)
	local exes = 'chrome'
	if active then
		os.execute('kill -STOP $(pidof '..exes..')')
	else
		os.execute('kill -CONT $(pidof '..exes..')')
	end
end)

--------------------------------------------------------------------------------

-- detect bots on the server
-- it just checks if their steam name is different and if they're on some lists

local json = require 'json'
local rex  = require 'rex_pcre2'

table.includes = function (t, v)
	for i = 1, #t do
		if t[i] == v then return true end
	end
	return false
end

local bad_steamids_old = {}
-- update ^ with a newly read name and steaid from status
local bad_steamids_check_status_entry = function (name, steamid)
	if name:find('nigger')
	or name:find('steam')
	or name:find('valve')
	or name:find('\n') then
		bad_steamids_old[steamid] = true
	end
end

local bad_steamids_milenko = {}
local bad_steamids_pazer = {}
local bad_names_pazer = {}
add_listener('startup', function ()
	-- read old statuses from the console log
	-- (it is small enough to read to a string)
	local f <close> = open_log()
	assert(f:seek('set', 0))
	local s = assert(f:read('a'))
	for id, name, steamid in rex.gmatch(s,
	    '# +([0-9]+) "((?:.|\n){0,32})"(?=.{32}) +(\\[U:1:[0-9]+\\]) +[0-9]+:[0-9]+(?::[0-9]+)? +[0-9]+ +[0-9]+ [a-z]+\n') do
		bad_steamids_check_status_entry(name, steamid)
	end
	-- https://github.com/PazerOP/tf2_bot_detector
	local f <close> = io.open(os.getenv('HOME')..'/git/tf2_bot_detector/staging/cfg/playerlist.official.json')
	if f then
		for _, t in ipairs(json.decode(f:read('a')).players) do
			if table.includes(t.attributes, 'cheater') then
				bad_steamids_pazer[t.steamid] = true
				if t.last_seen and t.last_seen.player_name then
					bad_names_pazer[t.last_seen.player_name:gsub('^%([0-9]+%)', '')] = true
				end
			end
		end
	end
	-- https://github.com/incontestableness/milenko
	local f <close> = io.open(os.getenv('HOME')..'/git/milenko/playerlist.milenko-list.json')
	if f then
		for _, t in ipairs(json.decode(f:read('a')).players) do
			bad_steamids_milenko[t.steamid] = true
		end
	end

	local count = function (t) local n = 0 for k in pairs(t) do n = n + 1 end return n end
	println('bad_steamids_old = %d', count(bad_steamids_old))
	println('bad_steamids_milenko = %d', count(bad_steamids_milenko))
	println('bad_steamids_pazer = %d', count(bad_steamids_pazer))
end)

local rex_match_and_remove = function (s, pat, ...)
	local t = {rex.find(s, pat, ...)}
	if t[1] ~= nil then
		local startpos, endpos = t[1], t[2]
		local s2 = ''
		if startpos > 1 then s2 = s2 .. s:sub(1, startpos) end
		if endpos < #s then s2 = s2 .. s:sub(endpos, #s) end
		return s2, select(3, table.unpack(t))
	end
	return s, nil
end

local cached_playerlist = nil
local cached_playerlist_time = 0

local get_playerlist = function ()
	if cached_playerlist and _ms() <= cached_playerlist_time+5000 then
		return cached_playerlist
	end

	cmd.status()

	local players = {}
	local count = nil

	-- ※ 注意点：
	-- the lines can come out of order and interspersed with other output
	-- because of that, the starting '#' might not be the first thing on a line
	-- this also needs to support names that contain newlines
	local data = ''
	for _, line in wait_for_events('game_console_output', 2000) do
		data = (data .. line .. '\n')

		local id, name, steamid
		data, id, name, steamid = rex_match_and_remove(data,
		    '# +([0-9]+) "((?:.|\n){0,32})"(?=.{32}) +(\\[U:1:[0-9]+\\]) +[0-9]+:[0-9]+(?::[0-9]+)? +[0-9]+ +[0-9]+ [a-z]+\n')
		if id then
			local accountid = steamid:match('^%[U:1:([0-9]+)%]$')
			local id64 = tonumber(accountid)+76561197960265728
			local t = {
				id = tonumber(id),
				name = name,
				steamid = steamid,
				steamid64 = id64,
			}
			table.insert(players, t)
			players[tostring(id64)] = t
		end

		local cnt
		data, cnt = rex_match_and_remove(data,
		    'players : ([0-9]+) humans, [0-9]+ bots \\([0-9]+ max\\)\n')
		if cnt then
			count = tonumber(cnt)
		end

		if count and #players == count then
			break
		end
	end

	if not count then
		cmd.echo('warning: failed to find the player count')
	elseif #players ~= count then
		cmd.echo('warning: failed to parse all the players')
	end

	cached_playerlist = players
	cached_playerlist_time = _ms()

	return players
end

local shellquote = function (s)
	return '\'' .. s:gsub('\'', '\'\\\'\'') .. '\''
end
local get_json = function (url)
	local p <close> = assert(io.popen('timeout 2 curl -s ' .. shellquote(url), 'r'))
	local s = p:read('a')
	return json.decode(s)
end

local summaries_url_fmt =
    'http://api.steampowered.com/ISteamUser/GetPlayerSummaries/v0002/' ..
    '?key=%s&steamids=%s'
local get_summaries = function (players)
	local id64s = {}
	for _, t in ipairs(players) do
		table.insert(id64s, t.steamid64)
	end
	local url = string.format(summaries_url_fmt,
	    os.getenv('STEAM_APIKEY'),
	    table.concat(id64s, ','))
	return get_json(url)
end

cmd.echof = function (fmt, ...)
	return cmd.echo(string.format(fmt, ...))
end

local printableish = function (name)
	name = name:gsub('[^ -~]+', '?')
	if name:find('^[ ?]*$') then
		name = '(unprintable)'
	end
	return name
end
cmd.bots = function ()
	panic = function (err)
		cmd.echo('an error occurred: ' .. err)
		cmd.voicemenu(2, 5)
	end
	if not os.getenv('STEAM_APIKEY') then
		cmd.echo('STEAM_APIKEY not set')
		cmd.echo('get it from https://steamcommunity.com/dev/apikey')
		return
	end

	local players = get_playerlist()
	if #players == 0 then
		return
	end

	local summaries = get_summaries(players)

	cmd.echo('')
	local found_any = false
	for _, t in ipairs(summaries.response.players) do
		local complain = function (fmt, ...)
			found_any = true
			cmd.echof('%s %s: ' .. fmt,
			    printableish(players[t.steamid].name),
			    players[t.steamid].steamid,
			    ...)
		end
		-- sub(): clip to 31 characters to match the in-game limit
		-- gsub(): remove leading # to match the way it gets removed from in-game names
		-- todo: may be truncating utf-8 characters differently
		if t.personaname:sub(1, 31):gsub('^#', '') ~= players[t.steamid].name then
			complain('steam name differs: %s', printableish(t.personaname))
		end
		if bad_steamids_old[players[t.steamid].steamid] then
			complain('steamid had an impossible name in the past')
		end
		if bad_steamids_pazer[players[t.steamid].steamid] then
			complain('steamid is included in the pazer list')
		end
		if bad_steamids_milenko[players[t.steamid].steamid] then
			complain('steamid is included in the milenko list')
		end
		if bad_names_pazer[players[t.steamid].name:gsub('^%([0-9]+%)', '')] then
			complain('in-game name is included in the pazer list')
		end
		if bad_names_pazer[t.personaname] then
			complain('steam name is included in the pazer list')
		end
		bad_steamids_check_status_entry(players[t.steamid].name, players[t.steamid].steamid)
	end
	if not found_any then
		cmd.echo('no bots found?')
		cmd.voicemenu(2, 4) -- well dont that beat off
	else
		cmd.voicemenu(2, 5)
	end
	cmd.echo('')
end

-- idea:
local choose = function (choices)
	local this_co = coroutine.running()
	for _, k in ipairs(choices) do
		cmd[k] = function ()
			coroutine.resume(this_co, k)
		end
	end
	local choice = coroutine.yield()
	for _, k in ipairs(choices) do
		cmd[k] = nil
	end
	return choice
end

-- kick players that are definitely probably bots
cmd.kptadpb = function ()
	for _, t in ipairs(get_playerlist()) do
		local name = t.name:lower()
		local bad = false
		-- steam rejects names with these words
		if name:find('nigger')
		or name:find('steam')
		or name:find('valve')
		or name:find('\n') then
			bad = true
		end
		if bad_steamids_old[t.steamid] then
			bad = true
		end
		if bad_steamids_pazer[t.steamid] then
			bad = true
		end
		if bad_steamids_milenko[t.steamid] then
			bad = true
		end
		if bad then
			cmdv.callvote('kick', string.format('%d cheating', t.id))
		end
		bad_steamids_check_status_entry(t.name, t.steamid)
	end
end
cmd.kob = cmd.kptadpb -- legacy alias

-- kick myeslf
cmd.kms = function ()
	local myname = cvar.name
	for _, t in ipairs(get_playerlist()) do
		if t.name == myname then
			cmdv.callvote('kick', t.id)
		end
		bad_steamids_check_status_entry(t.name, t.steamid)
	end
end

--------------------------------------------------------------------------------

cmd.echo('</script.lua>')
