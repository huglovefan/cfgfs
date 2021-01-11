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

add_listener('classchange', function (class)
	if class == 'sniper' then
		add_listener('+mouse1', on_mouse1_dn)
	else
		remove_listener('+mouse1', on_mouse1_dn)
	end
end)

--------------------------------------------------------------------------------

-- cvars

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
bind('f1',                              'incrementvar net_graph 0 6 6')
bind('f2',                              'screenshot')
bind('f3',                              '')
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
	if is_pressed['alt'] then
		if force_use_world_model then
			force_use_world_model = nil
		else
			force_use_world_model = 0
		end
		fire_event('slotchange', slot)
		return
	end
	cmd('+use_action_slot_item')
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
		os.execute('kill -STOP $(pidof '..exes..') 2>/dev/null')
	else
		os.execute('kill -CONT $(pidof '..exes..') 2>/dev/null')
	end
end)

--------------------------------------------------------------------------------

cmd.echo('</script.lua>')
