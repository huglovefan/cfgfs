cmd.echo('<script.lua>')

cfgfs.init_on_reload = not not gamedir

--------------------------------------------------------------------------------

-- crosshair and slot thing

do

class = global('class', 'scout')
slot = global('slot', 1)

xhs = global('xhs', {
	--               1            2            3      4      ... 5
	scout        = { {f=6},       {f=5, s=22}, {f=4}, m=2 },
	soldier      = { {f=6},       {f=7, s=24}, {f=4}, m=3 },
	pyro         = { {f=5, s=28}, {f=7, s=24}, {f=4}, m=3 },
	demoman      = { {f=2},       {f=7, s=24}, {f=4}, m=1 },
	heavyweapons = { {f=3},       {f=7, s=24}, {f=4} },
	-- build pda switches to melee on build
	-- destruct pda remembers active slot
	engineer     = { {f=6},       {f=5, s=22}, {f=4}, {sk=1, f=4}, {sk=1} },
	medic        = { {f=5, s=24}, {f=7, s=24}, {f=4} },
	sniper       = { {f=5, s=20}, {f=7, s=22}, {f=4} },
	-- disguise remembers active slot
	spy          = { {f=5, s=20}, {f=7, s=24}, {f=4}, {sk=1} },
	_v = 1,
})

local mod_prev = nil

local xh_update = function (slot)
	local t = xhs[class][slot]
	if not t then return end
	if t.f then
		cvar.cl_crosshair_file = 'crosshair'..t.f
		cvar.cl_crosshair_scale = t.s or 32
	end
end
add_listener('slotchange', xh_update)

local do_slot = function (n, is_mod, noskip)
	if not (n and xhs[class][n] and (noskip or not xhs[class][n].sk)) then
		return cfg('slot6') -- makes the "selection failed" sound
	end
	if not is_mod then slot = n end
	mod_prev = nil
	cfgf('slot%d', n)
	fire_event('slotchange', n)
end

local getslot = function (slot, add)
	slot = (slot or #xhs[class])
	local slmax = #xhs[class]
	for i = 1, slmax-1 do
		local n = ((slot-1+add*i)%slmax)+1
		if not xhs[class][n].sk then
			return n
		end
	end
	return nil
end
slotcmd = function (n)
	return function ()
		if is_pressed['ctrl'] and is_pressed['alt'] then
			xhs[class][n].sk = (not xhs[class][n].sk) or nil
			if n == slot and xhs[class][n].sk then
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
	local m = xhs[class].m
	if not m or m == slot then return cfg('slot6') end
	do_slot(m, true)
	mod_prev = slot
end
mod_up = function ()
	if mod_prev then
		do_slot(mod_prev)
	end
end

add_listener('classchange', function (cls)
	if cls == class then return end
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

-- i hate this

local jump_dn = '+jump;slot6;spec_mode'
local jump_up = '-jump'

if 1 + 1 == 3 then
	local dn = 100
	local jump = 697 -- 695? 697? 703? 706?
	local jumping = nil

	local cmd_dn = jump_dn
	local cmd_up = jump_up

	jump_dn = function ()
		if jumping then return end
		local t = {} jumping = t
		while true do
			cfg(cmd_dn)
			wait2(dn)
			if jumping ~= t then break end

			cfg(cmd_up)
			wait2(jump-dn)
			if jumping ~= t then break end
		end
	end
	jump_up = function ()
		jumping = nil
		return cfg(cmd_up)
	end
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

cmd.fortune = function ()
	for line in io.popen('fortune | cowsay'):lines() do
		cmd.echo(line)
	end
end

cmd.cfgfs_init = 'exec cfgfs/init'

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
cvar.hud_medicautocallersthreshold = 124
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

cvar.mat_disable_lightwarp = 0
cvar.r_lightaverage = 0
cvar.r_rimlight = 1

cvar.cl_crosshair_red = 255
cvar.cl_crosshair_green = 0
cvar.cl_crosshair_blue = 255

local checkwm = function ()
	local use_world_model = 1
	if class == 'sniper' and slot == 1 then
		use_world_model = 0 -- huntsman
	end
	if class == 'spy' then
		use_world_model = 0 -- cloak
	end
	cvar.cl_first_person_uses_world_model = use_world_model
end
add_listener('classchange', checkwm)
add_listener('slotchange', checkwm)

-- sto = 20
-- lux = 47
cvar.tf_mm_custom_ping_enabled = 1
cvar.tf_mm_custom_ping = 35

--------------------------------------------------------------------------------

-- binds

-- https://wiki.teamfortress.com/wiki/List_of_default_keys

cmd.unbindall()

bind('mouse1',			attack1_dn, attack1_up)
bind('mouse2',			attack2_dn, attack2_up)
bind('mouse3',			attack3_dn, attack3_up)
bind('mouse4',			'voicemenu 0 7') -- y*s
bind('mouse5',			'voicemenu 0 6') -- n*
bind('mwheeldown',		invnext)
bind('mwheelup',		invprev)

bind('escape',			'cancelselect')
bind('f1',			'incrementvar net_graph 0 6 6')
bind('f2',			'screenshot')
bind('f3',			'')
bind('f4',			'player_ready_toggle')
bind('f5',			'')
bind('f6',			'')
bind('f7',			'')
cmd.bind('f8',			'exec cfgfs/buffer')
cmd.bind('f9',			'exec cfgfs/buffer')
bind('f10',			function ()
				    local old = tonumber(cvar.con_enable)
				    cvar.con_enable = 1
				    cmd.toggleconsole()
				    cvar.con_enable = old
				end)
cmd.bind('f11',			'exec cfgfs/buffer')
bind('f12',			'')

bind('scrolllock',		'')
bind('pause',			'')

bind('\\',			'+use_action_slot_item')
bind('1',			slotcmd(1))
bind('2',			slotcmd(2))
bind('3',			slotcmd(3))
bind('4',			slotcmd(4))
bind('5',			slotcmd(5))
bind('6',			slotcmd(6))
bind('7',			slotcmd(7))
bind('8',			slotcmd(8))
bind('9',			slotcmd(9))
bind('0',			slotcmd(10))
bind('=',			'')
bind('[',			'')
bind('backspace',		'')

bind('ins',			'')
bind('home',			'')
bind('pgup',			'')

bind('tab',			'+showscores;cl_showfps 2;cl_showpos 1',
           			'-showscores;cl_showfps 0;cl_showpos 0')

bind('q',			cmd.fixme)
bind('w',			nullcancel_pair('w', 's', 'forward', 'back'))
bind('e',			'voicemenu 0 0')
cmd.bind('r',			'+reload') -- can't change disguise team otherwise
bind('t',			'')
cmd.bind('y',			'say') -- can't open chat otherwise
cmd.bind('u',			'say_team') -- can't open chat otherwise
bind('i',			'callvote')
bind('o',			'kill')
bind('p',			'explode')
bind(']',			'')
bind('semicolon',		'')
bind('enter',			'')

bind('del',			'')
bind('end',			'')
bind('pgdn',			'')

bind('capslock',		'')
bind('a',			nullcancel_pair('a', 'd', 'moveleft', 'moveright'))
bind('s',			nullcancel_pair('s', 'w', 'back', 'forward'))
bind('d',			nullcancel_pair('d', 'a', 'moveright', 'moveleft'))
bind('f',			'+inspect')
cmd.bind('g',			'+taunt') -- can't use normal taunt from menu otherwise
cmd.bind('h',			'lastinv') -- can't cancel taunt menu otherwise
bind('j',			'cl_trigger_first_notification')
bind('k',			'cl_decline_first_notification')
bind('l',			'dropitem')
bind('semicolon',		'')
bind("'",			'')
bind('/',			'')

bind('shift',			mod_dn, mod_up)
bind('z',			'voice_menu_1')
bind('x',			'voice_menu_2')
bind('c',			'voice_menu_3')
bind('v',			'noclip')
bind('b',			'lastdisguise')
bind('n',			'open_charinfo_backpack')
bind('m',			'open_charinfo_direct')
bind(',',			'changeclass')
bind('.',			'changeteam')
bind('-',			'')
bind('rshift',			'')

do

local lol = nil

fn_dn = function ()
	if lol then return end
	local t = {} lol = t
	while true do
		cfg('destroy 3;build 3')
		wait2(100)
		if lol ~= t then break end
	end
end
fn_up = function ()
	lol = nil
end

end

bind('ctrl',			'+duck')
bind('lwin',			'')
bind('alt',			'+strafe')
bind('space',			jump_dn,
             			jump_up)
bind('ralt',			'')
bind('rwin',			'')
bind('app',			'')
bind('rctrl',			'')

bind('uparrow',			'')
bind('downarrow',		'')
bind('leftarrow',		'')
bind('rightarrow',		'')

bind('numlock',			'')
bind('kp_slash',		'')
bind('kp_multiply',		'')
bind('kp_minus',		'')

bind('kp_home',			'') -- 7
bind('kp_uparrow',		'') -- 8
bind('kp_pgup',			'') -- 9
bind('kp_plus',			'')

bind('kp_leftarrow',		'') -- 4
bind('kp_5',			'') -- 5
bind('kp_rightarrow',		'') -- 6

bind('kp_end',			'') -- 1
bind('kp_downarrow',		'') -- 2
bind('kp_pgdn',			'') -- 3
bind('kp_enter',		'')

bind('kp_ins',			'')
bind('kp_del',			'')

cmd.echo('</script.lua>')
