-- cfgfs script for Team Fortress 2

cmd.echo('<script.lua>')

cfgfs.hide_cfgs['comfig/echo.cfg'] = true

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
			cancel_event()
			table.insert(xhs[class][slot], xhs[class][slot][1])
			table.remove(xhs[class][slot], 1)
			return xh_update(slot)
		else
			return cmd.play('common/wpn_denyselect.wav')
		end
	end
end)

local do_slot = function (n, is_mod, noskip)
	if not (n and xhs[class][n] and (noskip or not xhs[class][n].skip)) then
		return cmd.play('common/wpn_denyselect.wav')
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
	if not mod or mod == slot then
		return cmd.play('common/wpn_denyselect.wav')
	end
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

local wrap = function (terminal_command)
	return function ()
		for line in sh(terminal_command):lines() do
			cmd.echo(line)
		end
	end
end
cmd.fortune = wrap('fortune | cowsay')
cmd.top = wrap('top -bn1')

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

add_listener('+m', function ()
	if not is_pressed['ctrl'] then return end
	cancel_event()
	cvar.voice_enable = 1~tonumber(cvar.voice_enable)
end)

cmd.antiafk = function ()
	cfg('+moveleft')
	wait(100)
	cfg('-moveleft; +moveright')
	wait(200)
	cfg('-moveright; +moveleft')
	wait(100)
	cfg('-moveleft')
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
	cmd.play('player/recharged.wav')

	if wait_for_event({'-mouse1', '+mouse2', 'slotchange'}, 4000) then
		return
	end
	cmd.play('common/wpn_denyselect.wav')
	wait(250)
	cmd.play('common/wpn_denyselect.wav')
end

-- flare gun readiness bell

local flare_charging = false
local on_mouse1_dn_pyro = function ()
	if slot ~= 2 or flare_charging then return end

	while true do
		flare_charging = true
		wait(2000)
		flare_charging = false
		cmd.play('player/recharged.wav')
		-- fixme: also break if holding mod key (slot global is wrong)
		if not is_pressed['mouse1'] or slot ~= 2 then
			break
		end
	end
end

add_listener('classchange', function (class)
	if class == 'sniper' then
		add_listener('+mouse1', on_mouse1_dn)
	else
		remove_listener('+mouse1', on_mouse1_dn)
	end
	if class == 'pyro' then
		flare_charging = false
		add_listener('+mouse1', on_mouse1_dn_pyro)
	else
		remove_listener('+mouse1', on_mouse1_dn_pyro)
	end
end)

spinoff(function ()
	wait_for_event('classchange')
	local myname = cvar.name
	for _, t in wait_for_events('player_killed') do
		if t.killer == myname and t.weapon:find('deflect') then
			wait(300)
			cmd.voicemenu(2, 6) -- nice shot!
		end
	end
end)

--------------------------------------------------------------------------------

-- cvars

cvar.sensitivity = 2.6*2 -- https://github.com/ValveSoftware/Source-1-Games/issues/1834

cvar.snd_musicvolume = 0
cvar.voice_scale = 0.3
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
cvar.tf_contract_progress_show = 0 -- hide contract hud
cvar.tf_dingaling_volume = 1
cvar.tf_dingalingaling = 1
cvar.tf_hud_target_id_disable_floating_health = 1
cvar.tf_remember_activeweapon = 1
cvar.tf_respawn_on_loadoutchanges = 0
cvar.tf_scoreboard_mouse_mode = 2
cvar.tf_scoreboard_ping_as_text = 1
cvar.tf_sniper_fullcharge_bell = 1
cvar.tf_use_match_hud = 0
cvar.viewmodel_fov = 75
cvar.voice_overdrive = 1 -- don't duck game sounds when someone speaks

cvar.mat_disable_lightwarp = 0
cvar.r_lightaverage = 0
cvar.r_rimlight = 1

cvar.r_dynamic = 0

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

--------------------------------------------------------------------------------

-- binds

-- https://wiki.teamfortress.com/wiki/List_of_default_keys

-- things that need the key bound directly:
-- * +reload for switching disguise team
-- * say, say_team for using the chat
-- * +taunt to exit group taunts and use the normal taunt from the menu
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
bind('f3',                              '+use_action_slot_item')
bind('f4',                              'player_ready_toggle')
bind('f5',                              '')
bind('f6',                              '')
bind('f7',                              '')
bind('f8',                              '')
bind('f9',                              '')
bind('f10', function ()
	cvar.con_enable = 1
	cmd.toggleconsole()
	cvar.con_enable = 0
end)
bind('f11',                             'cfgfs_click')
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
bind('q',                               function (...) return cmd.kob(...) end)
bind('w',                               nullcancel_pair('w', 's', 'forward', 'back'))
bind('e',                               'voicemenu 0 0')
cmd.bind('r',                           '+reload')
bind('t', function (_, key)
	if is_pressed['alt'] then
		if force_use_world_model then
			force_use_world_model = nil
		else
			force_use_world_model = 0
		end
		fire_event('slotchange', slot)
		return
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
		sh('exec killall -q -STOP '..exes)
	else
		sh('exec killall -q -CONT '..exes)
	end
end)

spinoff(function ()
	wait_for_event('classchange')

	-- set threads' cpu affinities now that we're pretty sure they've all
	--  been created
	-- putting MatQueue0 and the mesa threads on the same core makes the
	--  mastercomfig benchmark demo run 5% faster (100fps -> 105)
	sh([[
	exec >/dev/null 2>&1
	threads $(pidof hl2_linux) | awk '{print $2}' | xargs -rn1 taskset -c -p 0,1
	threads $(pidof hl2_linux) | awk '$3=="MatQueue0"||/:/{print $2}' | xargs -rn1 taskset -c -p 2,3
	threads $(pidof cfgfs)     | awk '{print $2}' | xargs -rn1 taskset -c -p 0,1
	doas cpupower -c 2,3 frequency-set --max 3.50GHz
	]])
end)

--------------------------------------------------------------------------------

bad_steamids = global('bad_steamids', {})
oldcnt = global('oldcnt', -1)

listupdate = function ()
	bad_steamids = {}
	local cnt = 0
	for line in sh('timeout 10 sh misc/get_bad_steamids.sh'):lines() do
		local steamid, lists = line:match('^(%S+)%s+(%S+)$')
		if steamid then
			bad_steamids[steamid] = lists
			cnt = cnt+1
		else
			eprintln('listupdate: failed to parse line: "%s"', line)
		end
	end
	if oldcnt ~= -1 then
		println('listupdate: loaded %d steamids (%d new)',
		    cnt, cnt-oldcnt)
	else
		println('listupdate: loaded %d steamids', cnt)
	end
	oldcnt = cnt
end

if not os.getenv('NO_LISTUPDATE') then
	add_listener('startup', function ()
		panic = fatal
		listupdate()
		panic = nil
	end)
end

local parse_status = function ()
	local players = {}
	local expcnt = nil
	for _, line in wait_for_events('game_console_output', 1000) do
		if not expcnt then
			local                      humans =
			    line:match('^players : ([0-9]+) humans, [0-9]+ bots %([0-9]+ max%)$')
			if humans then
				expcnt = tonumber(humans)
				cancel_event()
				goto matched
			end
		end
		do
			local               userid,    name,  uniqueid,         connected, ping,     loss,     state =
			    line:match('^# +([0-9]+) +"(.+)" +(%[U:1:[0-9]+%]) +([0-9:]+) +([0-9]+) +([0-9]+) +([a-z]+)$')
			if userid and not players[uniqueid] then
				table.insert(players, {
					userid  = tonumber(userid),
					name    = name,
					steamid = uniqueid,
				})
				players[uniqueid] = players[#players]
				cancel_event()
				goto matched
			end
		end
		if (
			line == '# userid name                uniqueid            connected ping loss state' or
			line:find('^hostname: .*$') or
			line:find('^version : .*/%-?[0-9]+ %-?[0-9]+ .*$') or
			line:find('^udp/ip  : .*:%-?[0-9]+.*$') or
			line:find('^steamid : .* %([0-9]+%)$') or
			line:find('^steamid : not logged in$') or
			line:find('^account : logged in.*$') or
			line:find('^account : not logged in.*$') or
			line:find('^map     : .* at: %-?[0-9]+ x, %-?[0-9]+ y, %-?[0-9]+ z$') or
			line:find('^tags    : .*$') or
			line:find('^sourcetv:  port %-?[0-9]+, delay %-?[0-9]+%.[0-9]+s$') or
			line:find('^replay  :  .*$') or
			line:find('^players : %-?[0-9]+ humans, %-?[0-9]+ bots %(%-?[0-9]+ max%)$') or
			line:find('^edicts  : %-?[0-9]+ used of %-?[0-9]+ max$')
		) then
			cancel_event()
		end
		goto next
		::matched::
		if expcnt and #players == expcnt then
			break
		end
		::next::
	end
	if #players == 0 and not expcnt then
		cmd.echo('parse_status: couldn\'t parse anything!')
	elseif not expcnt then
		cmd.echo('parse_status: couldn\'t find expected player count')
	elseif #players ~= expcnt then
		cmd.echo(string.format('parse_status: could only parse %d of %d players', #players, expcnt))
	end
	table.sort(players, function (p1, p2)
		return p1.userid < p2.userid
	end)
	return players
end

local copy_steamnames = function (oldstatus, newstatus)
	if oldstatus then
		for _, t in ipairs(oldstatus) do
			if newstatus[t.steamid] then
				newstatus[t.steamid].steamname = t.steamname
			end
		end
	end
	return newstatus
end

local last_status = nil
local last_status_time = 0
local status_updating = false
local do_status = function (cached_only)
	if (
		(last_status and _ms()-last_status_time <= 5000) or
		cached_only
	) then
		return (last_status or {})
	end
	if status_updating then
		local _, rv = wait_for_event('status_updated')
		return rv
	end
	status_updating = true
	cmd.status()
	last_status = copy_steamnames(last_status, parse_status())
	last_status_time = _ms()
	status_updating = false
	fire_event('status_updated', last_status)
	return last_status
end

local teamnames = {
	['TF_GC_TEAM_DEFENDERS'] = 'red',
	['TF_GC_TEAM_INVADERS']  = 'blu',
}
local parse_lobby_debug = function ()
	local players = {}
	local mexp, pexp
	local mcnt, pcnt = 0, 0
	for _, line in wait_for_events('game_console_output', 1000) do
		if not mexp and #players == 0 then
			if line == 'Failed to find lobby shared object' then
				break
			end
		end
		if not mexp and #line >= 59 then
			local                                          mexp_,              pexp_ =
			    line:match('^CTFLobbyShared: ID:[0-9a-f]+  ([0-9]+) member%(s%), ([0-9]+) pending$')
			if mexp_ then
				mexp, pexp = tonumber(mexp_), tonumber(pexp_)
				cancel_event()
				goto matched
			end
		end
		if #line >= 57 then
			local              state,        statepos,   steamid,                 team,             type =
			    line:match('^  ([A-Z][a-z]+%[([0-9]+)%]) (%[U:1:[0-9]+%])  team = (TF_GC_TEAM_[A-Z_]+)  type = ([A-Z_]+_PLAYER)$')
			if state and not players[steamid] then
				if state:find('^Member%[') then mcnt = mcnt+1
				elseif state:find('^Pending%[') then pcnt = pcnt+1
				else goto next end
				table.insert(players, {
					state    = state:match('^[A-Z][a-z]+'),
					statepos = tonumber(statepos),
					steamid  = steamid,
					team     = assert(teamnames[team], 'invalid team ' .. team),
				})
				players[steamid] = players[#players]
				cancel_event()
				goto matched
			end
			goto next
		end
		::matched::
		if mcnt == mexp and pcnt == pexp then
			break
		end
		::next::
	end
	if #players == 0 and not (mexp and pexp) then
		cmd.echo('parse_lobby_debug: couldn\'t parse anything!')
	elseif not (mexp and pexp) then
		cmd.echo('parse_lobby_debug: couldn\'t find expected player counts')
	elseif not (mcnt == mexp and pcnt == pexp) then
		cmd.echo(string.format('parse_lobby_debug: could only parse %d of %d members, %d of %d pending',
		    mcnt, mexp, pcnt, pexp))
	end
	table.sort(players, function (p1, p2)
		if p1.state == p2.state then
			return p1.statepos < p2.statepos
		else
			return p1.state < p2.state
		end
	end)
	return players
end

local last_lobby_debug = nil
local last_lobby_debug_time = 0
local lobby_debug_updating = false
local do_lobby_debug = function (cached_only)
	if (
		(last_lobby_debug and _ms()-last_lobby_debug_time <= 1000) or
		cached_only
	) then
		return (last_lobby_debug or {})
	end
	if lobby_debug_updating then
		local _, rv = wait_for_event('lobby_debug_updated')
		return rv
	end
	lobby_debug_updating = true
	cmd.tf_lobby_debug()
	last_lobby_debug = parse_lobby_debug()
	last_lobby_debug_time = _ms()
	lobby_debug_updating = false
	fire_event('lobby_debug_updated', last_lobby_debug)
	return last_lobby_debug
end

local get_playerlist = function (cached_only)
	if cached_only == nil and not game_window_is_active() then
		cached_only = true
	end
	local players = {}
	local lobby = do_lobby_debug(cached_only)
	if #lobby > 0 then
		local status = do_status(cached_only)
		for _, t in ipairs(status) do
			if lobby[t.steamid] then
				team_known(t.name, lobby[t.steamid].team)
				table.insert(players, {
					steamid = t.steamid,
					name    = t.name,
					userid  = t.userid,
					team    = lobby[t.steamid].team,
				})
				players[t.steamid] = players[#players]
			end
		end
	end
	return players
end

string.escape = function (s)
	local t = {}
	local i = 1
	local other_escapes = {
		[0x09] = '\\t',
		[0x0a] = '\\n',
		[0x0d] = '\\r',
		[0x5c] = '\\\\',
	}
	while i <= #s do
		-- note: the range is \x20-\x7e excluding backslash (\x5c)
		local m = s:match('^[\x5d-\x7e\x20-\x5b]+', i)
		if m then
			t[#t+1] = m
			i = i+#m
			goto next
		end
		-- note: this one is utf8.charpattern without ascii chars
		local m = s:match('^[\xc2-\xfd][\x80-\xbf]*', i)
		if m then
			if utf8.len(m) then
				local p = utf8.codepoint(m)
				if p <= 0xffff
				then t[#t+1] = string.format('\\u{%04x}', p)
				else t[#t+1] = string.format('\\u{%06x}', p)
				end
			else
				for i = 1, #m do
					t[#t+1] = string.format('\\x%02x', m:byte(i))
				end
			end
			i = i+#m
			goto next
		end
		do
			local b = s:byte(i)
			t[#t+1] = other_escapes[b] or string.format('\\x%02x', b)
			i = i+1
		end
		::next::
	end
	return table.concat(t, '')
end

positive_vocalization = function () cmd.voicemenu(2, 4) end
negative_vocalization = function () cmd.voicemenu(2, 5) end

local check_namestealer = function (t, playerlist)
	local names = {}
	for _, t in ipairs(playerlist) do
		names[t.name] = t
	end

	local name = t.name

	if utf8.len(name) then
		for p, c in utf8.codes(name) do
			if (c <= 0x1f and not (c == 0x09)) or c == 0x7f or c >= 0x80 then
				local without = (name:sub(1, p-1) .. name:sub(p+#utf8.char(c)))
				assert(without ~= name)
				if without ~= '' and names[without] then
					assert(names[without].steamid ~= t.steamid)
					return true
				end
			end
		end
	end

	if (
		name:find('.\u{200f}$') or
		name:find('.\u{202d}$') or
		name:find('.\u{0e4a}$')
	) then
		return true
	end

	return false
end

local mangle_name = function (name)
	name = name:sub(1, 31)
	name = name:gsub('%%', '')
	name = name:gsub('~', '')
	name = name:gsub('^#', '', 1)
	return name
end

local check_bot = function (t, players, grasp_at_straws)
	local reasons = {}
	do
		local clean = t.name
		    :gsub('[\x7f-\xff]', '')
		    :lower()
		    :frobnicate()
		if (
			clean:find(' ', 1, true) or
			clean:find('DCMMOX', 1, true) or
			clean:find('\\KF\\O', 1, true)
		) then
			table.insert(reasons, 'impossible name')
		end
	end
	do
		local clean = t.name
		    :gsub('^%([0-9]+%)', '', 1)
		    :gsub('[\x7f-\xff]', '')
		    :frobnicate()
		if (
			clean == 'YBEZZS\x04MM\x05jN\x19LI\x1aD\x1c' or
			clean == '~]CFCMB^\nyZKXAFO\nR\n\x1e\x18' or
			clean == 'egomk~xedci' or
			clean == 'nEI^EX\ndMMOXACFFOX' or
			clean == 'aCDM\ndMMOXACFFOX' or
			clean == 'CXKCPE\x04YOFFS\x04Y^EXO'
		) then
			table.insert(reasons, 'known bot name')
		end
	end
	if check_namestealer(t, players) then
		table.insert(reasons, 'name stealer')
	end
	if bad_steamids[t.steamid] then
		table.insert(reasons, 'on lists: ' .. bad_steamids[t.steamid])
	end
	if grasp_at_straws and t.steamname then
		local mangled = mangle_name(t.steamname)
		if not (
			mangled == t.name or
			mangled == t.name:gsub('^%([0-9]+%)', '', 1)
		) then
			table.insert(reasons, string.format(
			    'steam name differs: %s',
			    string.escape(t.steamname)))
		end
	end
	if #reasons == 0 then return nil end
	return reasons
end

cmd.kob = function (_, keyname)
	local key_press_time = (keyname and rawget(is_pressed, keyname))

	local players = get_playerlist()
	if #players == 0 then
		cmd.echo('kob: failed to get playerlist!')
		return
	end

	local myname = cvar.name
	local myteam = nil
	for _, t in ipairs(players) do
		if t.name == myname then
			myteam = t.team
			break
		end
	end

	local our_bots = {}
	local our_humans = {}
	local their_bots = {}

	for _, t in ipairs(players) do
		local is_bot = check_bot(t, players)
		if is_bot then
			cmd.echo(string.format('%s: %s %s: %s',
			    t.team, string.escape(t.name), t.steamid,
			    table.concat(is_bot, '; ')))
			if t.team == myteam then
				table.insert(our_bots, t)
			else
				table.insert(their_bots, t)
			end
		else
			if t.team == myteam then
				table.insert(our_humans, t)
			end
		end
	end

	if #our_bots == 0 then
		if #their_bots == 0 then
			cmd.echo('no bots found')
			positive_vocalization()
		else
			cmd.echo('no bots found, but their team has ' .. #their_bots)
			negative_vocalization()
		end
		return
	end

	if #our_humans < 5 then
		cmd.echo('not enough participants for a vote to pass')
		negative_vocalization()
		return
	end

	if #our_bots > #our_humans then
		cmd.echo('bots outnumber humans, vote will fail if all of them vote no')
		negative_vocalization()
		return
	end

	while true do
		for i, t in ipairs(our_bots) do
			cmd.callvote('kick', string.format('%d cheating', t.userid))
			if i < #our_bots then
				wait(100)
			end
		end
		if key_press_time then
			if is_pressed[keyname] ~= key_press_time then
				break
			end
			wait(100)
			if is_pressed[keyname] ~= key_press_time then
				break
			end
		else
			break
		end
	end
end

local shellquote = function (s)
	return '\'' .. s:gsub('\'', '\'\\\'\'') .. '\''
end
local get_json = function (url)
	local s = sh('timeout 5 curl -s ' .. shellquote(url)):read('a')
	local json = require 'json' -- https://github.com/rxi/json.lua
	local ok, rv = pcall(json.decode, s)
	return ok and rv or nil
end

local summaries_url_fmt =
    'http://api.steampowered.com/ISteamUser/GetPlayerSummaries/v0002/' ..
    '?key=%s&steamids=%s'

local add_steamnames = function (players)
	if #players == 0 then return end
	if not os.getenv('STEAM_APIKEY') then return end
	if not pcall(require, 'json') then return end

	local all_have_steamname = true
	for _, t in ipairs(players) do
		if not t.steamname then
			all_have_steamname = false
			break
		end
	end
	if all_have_steamname then return end

	local id64s = {}
	for _, t in ipairs(players) do
		if not t.steamname then
			local accountid = tonumber(t.steamid:match('^%[U:1:([0-9]+)%]$'))
			local id64 = accountid+76561197960265728
			id64s[tostring(id64)] = t
			table.insert(id64s, id64)
		end
	end

	local url = string.format(summaries_url_fmt,
	    os.getenv('STEAM_APIKEY'),
	    table.concat(id64s, ','))

	local data = get_json(url)

	if type(data) == 'table'
	and type(data.response) == 'table'
	and type(data.response.players) == 'table' then
		for _, t in ipairs(data.response.players) do
			if type(t.steamid) == 'string'
			and id64s[t.steamid]
			and type(t.personaname) == 'string' then
				id64s[t.steamid].steamname = t.personaname
			end
		end
	end
end

cmd.lob = function ()
	local lobby = do_lobby_debug()
	local players = get_playerlist()
	if #players == 0 then
		cmd.echo('lob: failed to get playerlist!')
		return
	end

	add_steamnames(players)

	local playing = {red = 0, blu = 0}
	for _, t in ipairs(players) do
		local reasons = check_bot(t, players, true)
		if reasons then
			cmd.echo(string.format('%s: %s %s: %s',
			    t.team, string.escape(t.name), t.steamid,
			    table.concat(reasons, '; ')))
			playing[t.team] = playing[t.team]+1
		end
	end

	local joining = {red = 0, blu = 0}
	for _, t in ipairs(lobby) do
		if t.state == 'Pending' and bad_steamids[t.steamid] then
			joining[t.team] = joining[t.team]+1
		end
	end

	local msg = {}
	local total = 0
	for _, t in ipairs({
		{fmt='%d on red', cnt=playing.red},
		{fmt='%d on blu', cnt=playing.blu},
		{fmt='%d joining red', cnt=joining.red},
		{fmt='%d joining blu', cnt=joining.blu},
	}) do
		if t.cnt > 0 then
			table.insert(msg, string.format(t.fmt, t.cnt))
			total = total+t.cnt
		end
	end

	if #msg > 0 then
		cmd.echo(string.format('bots: %s (%d total)',
		    table.concat(msg, ', '),
		    total))
	else
		cmd.echo('no bots found?')
	end
end

--------------------------------------------------------------------------------

cmd.echo('</script.lua>')
