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

	medic        = { mod=1,
	                 {file=5, size=24},
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

-- footwork

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
	local hold_ms = 650

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

add_listener({'+1', '+2', '+3'}, function (_, key)
	cmd.next_map_vote(tonumber(key)-1)
end)

cmd.mute = function () cvar.voice_enable = 0 end
cmd.unmute = function () cvar.voice_enable = 1 end

do
	local myname = nil
	local pat = nil
	add_listener('classchange', function ()
		myname = (cvar.name or myname)
		if myname then
			pat = '^'..myname:gsub('[^A-Za-z0-9_]', '.')..' killed '
		end
	end)
	add_listener('game_console_output', function (line)
		if pat and line:find(pat) and line:find(' %(crit%)$') then
			cmd('+use_action_slot_item')
		end
	end)
end

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

-- the logfile needs to be reloaded if cfgfs was restarted while the game was running
-- sometimes the game will freeze if this is done improperly
-- (still wondering what the proper way is)
cmd.cfgfs_init_log = function ()
	reexec()
	cvar.con_logfile = ''
	cvar.con_logfile = 'custom/!cfgfs/cfg/console.log'
end
cfg('cfgfs_init_log')

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

local force_use_world_model = nil
add_listener({'classchange', 'slotchange'}, function ()
	local use_world_model = 1
	if not force_use_world_model then
		if class == 'heavyweapons' and slot == 1 then
			use_world_model = 0 -- minignu
		end
		if class == 'sniper' and slot == 1 then
			use_world_model = 0 -- huntsman
		end
		if class == 'spy' then
			use_world_model = 0 -- cloak
		end
	else
		use_world_model = force_use_world_model
	end
	cvar.cl_first_person_uses_world_model = use_world_model
end)

-- sto = 20
-- lux = 47
cvar.tf_mm_custom_ping_enabled = 1
cvar.tf_mm_custom_ping = 35

-- get the demo file from https://docs.mastercomfig.com/en/latest/benchmarks/
cmd.cfgfs_benchmark = function ()
	os.execute('prios')

	cmd.alias('game_overrides_once')

	local do_perf = true
	local perf_cmd = 'v doas perf record -p $(pidof hl2_linux)'
	local pid = ''
	local starting = function ()
		if not do_perf then return end
		pid = io.popen(perf_cmd .. ' >&2 & echo $!'):read('a')
	end
	local stopping = function ()
		if not do_perf then return end
		os.execute('v kill ' .. pid)
	end

	cmd.timedemo('benchmark_test')

	local ready = false
	for _, line in wait_for_events('game_console_output') do
		if not ready then
			if line == "'user/demoman.cfg' not present; not executing." then
				starting()
				ready = true
			end
		else
			if line:find('^4812 frames ') then
				stopping()
				break
			end
		end
	end

	cmd.quit()
end

--------------------------------------------------------------------------------

-- binds

-- https://wiki.teamfortress.com/wiki/List_of_default_keys

-- things that need the key bound directly:
-- * +reload for switching disguise team
-- * say, say_team for using the chat
-- * +taunt to exit group taunts and use the normal one from the menu
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
bind('f1',                              'incrementvar net_graph 0 6 6; vote option1')
bind('f2',                              'screenshot; vote option2')
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
bind('q', function (...)
	if is_pressed['alt'] then
		cmd.bots(...)
	else
		cmd.kptadpb(...)
	end
end)
bind('w',                               nullcancel_pair('w', 's', 'forward', 'back'))
bind('e',                               'voicemenu 0 0')
cmd.bind('r',                           '+reload')
bind('t', function (_, key)
	cmd('+use_action_slot_item')
	if is_pressed['alt'] then
		if force_use_world_model then
			force_use_world_model = nil
		else
			force_use_world_model = 0
		end
		fire_event('slotchange', slot)
	end
	repeat
		cmd.extendfreeze()
	until wait_for_event('-'..key, 1950)
end)
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
cmd.bind('g', '+taunt')
cmd['+taunt'] = function ()
	repeat
		cfg('cmd taunt')
	until wait_for_event('-taunt', 100/3)
end
cmd['-taunt'] = function ()
	fire_event('-taunt')
end
cmd.bind('h',                           'lastinv')
bind('j',                               'cl_trigger_first_notification')
bind('k',                               'cl_decline_first_notification')
bind('l', function (_, key)
	repeat
		cfg('cmd dropitem')
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

local json = require 'json'

-- detect bots on the server
-- it mostly just checks if they're on some lists

local bad_steamids = {}
add_listener('startup', function ()
	panic = fatal
	local p <close> = assert(io.popen('sh misc/get_bad_steamids.sh'))
	local cnt = 0
	for line in p:lines() do
		local steamid, lists = assert(line:match('^(%S+)%s+(%S+)$'))
		bad_steamids[steamid] = lists
		cnt = cnt+1
	end
	println('bad_steamids: loaded %d of them', cnt)
end)
local bad_steamids_check_status_entry = function (name, steamid)
	name = name:lower()
	if name:find('nigger')
	or name:find('steam')
	or name:find('valve')
	or name:find('\n') then
		if bad_steamids[steamid] then
			if not (','..bad_steamids[steamid]..','):find(',console%.log,') then
				bad_steamids[steamid] = 'console.log,'..bad_steamids[steamid]
			end
		else
			bad_steamids[steamid] = 'console.log'
		end
	end
end

--[[ get_playerlist ]] do

local playerlist = {}

local playerlist_updated = 0
local playerlist_currently_updating = false

local playerlist_cache_time <const> = 5*1000

get_playerlist = function ()
	if playerlist and _ms() <= playerlist_updated+playerlist_cache_time then
		return playerlist
	end

	if playerlist_currently_updating then
		wait_for_event('playerlist_updated')
		assert(not playerlist_currently_updating)
		return get_playerlist(steamid)
	end
	playerlist_currently_updating = true
	local oldpanic = panic; panic = fatal

	-- from run_capturing_output() in builtin.lua
	if not _in_get_contents then
		reexec()
		assert(_in_get_contents, 'timeouts done from wrong thread?')
	end
	cmd.status()

	local players = {}
	local count = nil

	for _, line in wait_for_events('game_console_output', 2000) do
		local id, name, steamid = line:match('^# +([0-9]+) +"(.*)" +(%[U:1:[0-9]+%]) +[0-9:]+ +[0-9]+ +[0-9]+ [^ ]+$')
		if id then
			local accountid = steamid:match('^%[U:1:([0-9]+)%]$')
			local id64 = tonumber(accountid)+76561197960265728
			name = name:gsub('^%([0-9]+%)', '')
			local t = {
				id = tonumber(id),
				name = name,
				steamid = steamid,
				steamid64 = id64,
			}
			table.insert(players, t)
			players[tostring(id64)] = t
		end

		local cnt = line:match('^players : ([0-9]+) humans, [0-9]+ bots %([0-9]+ max%)$')
		if cnt then
			count = tonumber(cnt)
		end

		if count and #players == count then
			break
		end
	end

	playerlist = players
	playerlist_updated = _ms()
	playerlist_currently_updating = false

	if not count then
		cmd.echo('warning: failed to find the player count')
	elseif #players ~= count then
		cmd.echo('warning: failed to parse all the players')
	end

	panic = oldpanic
	fire_event('playerlist_updated')

	return players
end

end -- get_playerlist

--[[ get_team ]] do

local teamdata = {}

local teamdata_updated = 0
local teamdata_currently_updating = false

local teamdata_cache_time <const> = 1000

local teamnames = {
	['TF_GC_TEAM_INVADERS'] = 'red',
	['TF_GC_TEAM_DEFENDERS'] = 'blu',
}
get_team = function (steamid)
	if _ms() <= teamdata_updated+teamdata_cache_time then
		return teamdata[steamid]
	end

	if teamdata_currently_updating then
		wait_for_event('teamdata_updated')
		assert(not teamdata_currently_updating)
		return get_team(steamid)
	end
	teamdata_currently_updating = true
	local oldpanic = panic; panic = fatal

	local t = {}
	for _, line in ipairs(run_capturing_output('tf_lobby_debug')) do
		local n, steamid, team = line:match('  [A-Z][a-z]+%[([0-9]+)%] ([^ ]+)  team = ([^ ]+)  type = [^ ]+$')
		if n then
			t[steamid] = (teamnames[team] or nil)
		end
	end

	teamdata = t
	teamdata_updated = _ms()
	teamdata_currently_updating = false

	panic = oldpanic
	fire_event('teamdata_updated')

	return t[steamid]
end

end -- get_team
-- ...
-- maybe this should be used instead of status for the main player list
-- should only use status for names & have the caching work like this function

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
	if not name:find('[ -~]') then
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
		local mangle_name_for_comparing = function (name)
			name = name:gsub('%%', '')
			name = name:gsub('~', '')
			name = name:gsub('^#', '', 1)
			-- maximum length on steam is 32, in-game it's 31
			-- ignore the last 4 bytes in case it's a utf-8 character that got truncated
			-- (not sure if this happens, haven't tested it)
			name = name:sub(1, 32-4)
			return name
		end
		if mangle_name_for_comparing(t.personaname)
		   ~= mangle_name_for_comparing(players[t.steamid].name) then
			complain('steam name differs: %s', printableish(t.personaname))
		end
		if bad_steamids[players[t.steamid].steamid] then
			complain('on lists: %s', bad_steamids[players[t.steamid].steamid])
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

local find_own_steamid do
	local own_steamid = nil
	find_own_steamid = function (playerlist) -- local
		if own_steamid then
			return own_steamid
		end
		local myname = cvar.name or cvar.name or cvar.name -- retry lol
		for _, t in ipairs(playerlist) do
			if t.name == myname then
				own_steamid = t.steamid
				return t.steamid
			end
		end
		return nil
	end
end

-- kick players that are definitely probably bots
cmd.kptadpb = function (_, key)
	panic = function (err)
		cmd.echof('kptadpb: %s', err)
		cmd.voicemenu(2, 5)
	end
	local bind_press_time = (type(key) == 'string' and rawget(is_pressed, key))

	local playerlist = get_playerlist()
	local mysteamid = assert(find_own_steamid(playerlist), 'failed to get own steamid')
	local myteam = assert(get_team(mysteamid), 'failed to get own team')
	local bots = {}
	local myteam_humans = 0
	local myteam_robots = 0

	local check_lists = function (t)
		return (nil ~= bad_steamids[t.steamid])
	end

	local check_impossible_names = function (t)
		local name = t.name:lower()
		-- steam rejects names with these words
		if name:find('nigger')
		or name:find('steam')
		or name:find('valve')
		or name:find('\n') then
			return true
		end
		return false
	end

	local names = {}
	for _, t in ipairs(playerlist) do
		names[t.name] = true
	end
	local check_namestealer = function (t)
		local name = t.name
		name = name:gsub('\xe2\x80\x8f', '')
		return (name ~= t.name and names[name])
	end

	for _, t in ipairs(playerlist) do
		local team = get_team(t.steamid) --, 'failed to get team for '..t.steamid)
		if (not team) or team == myteam then
			local is_bot = check_lists(t)
			            or check_impossible_names(t)
			            or check_namestealer(t)
			if is_bot then
				table.insert(bots, t)
				myteam_robots = (myteam_robots + 1)
			else
				myteam_humans = (myteam_humans + 1)
			end
		end
		bad_steamids_check_status_entry(t.name, t.steamid)
	end
	-- is the vote doomed to fail?
	-- this is to avoid wasting votes since there's a cooldown
	local doomed = false
	if #bots > 0 then
		if myteam_humans < 5 then
			-- votes need at least 6 participants to work
			-- target automatically votes no for itself, so need to have at least 5 competent humans to vote yes
			doomed = true
			cmd.echof('kptadpb: vote won\'t pass (not enough participants)')
			cmd.voicemenu(2, 5)
		end
		if not (myteam_humans > myteam_robots) then
			-- need to have at least one more yes vote than no votes for the vote to pass
			-- this vote will fail assuming all the bots vote no
			doomed = true
			cmd.echof('kptadpb: vote won\'t pass (bots outnumber humans)')
			cmd.voicemenu(2, 5)
		end
	end
	if #bots > 0 and not doomed then
		for _, t in ipairs(bots) do
			cmdv.callvote('kick', string.format('%d cheating', t.id))
		end
		if bind_press_time and bind_press_time == is_pressed[key] then
			local i = 1
			while not wait_for_event('-'..key, 30) do
				if i == #bots+1 then i = 1 end
				cmdv.callvote('kick', string.format('%d cheating', bots[i].id))
				i = i + 1
			end
		end
	else
		if #bots == 0 then
			cmd.voicemenu(2, 4)
		end
		if bind_press_time and bind_press_time == is_pressed[key] then
			if not wait_for_event('-'..key, 100) then
				return cmd.kptadpb(_, key)
			end
		end
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
