cmd.echo('<script_fof.lua>')

cfgfs.game_window_title_is('Fistful of Frags - OpenGL')
cfgfs.compat_noalias = true -- https://steamcommunity.com/games/fof/announcements/detail/199616928162078893
cfgfs.init_after_cfg['config.cfg'] = true -- why does it sometimes not do autoexec?

--------------------------------------------------------------------------------

cvar.con_logfile = 'console.log'

cvar.sensitivity = 2.6

cvar.snd_musicvolume = 0
cvar.voice_scale = 0.5
cvar.volume = 1

cvar.cl_disablehtmlmotd = 1
cvar.cl_rumblescale = 0
cvar.con_enable = 0
cvar.engine_no_focus_sleep = 100
cvar.fov_desired = 90
do local refresh_rate = 60/1.001
   cvar.fps_max = refresh_rate*2+1 end
cvar.m_rawinput = 1
cvar.snd_mute_losefocus = 0

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

bind('w',                               nullcancel_pair('w', 's', 'forward', 'back'))
bind('a',                               nullcancel_pair('a', 'd', 'moveleft', 'moveright'))
bind('s',                               nullcancel_pair('s', 'w', 'back', 'forward'))
bind('d',                               nullcancel_pair('d', 'a', 'moveright', 'moveleft'))

bind('tab',                             '+showscores;cl_showfps 2;cl_showpos 1',
                                        '-showscores;cl_showfps 0;cl_showpos 0')

cmd.bind('f8',                          'exec cfgfs/click')
bind('f9',                              release_all_keys)

bind('f10', function ()
	local old = cvar.con_enable
	cvar.con_enable = 1
	cmd.toggleconsole()
	cvar.con_enable = old
end)

cmd.bind('f11',                         'exec cfgfs/click')

--------------------------------------------------------------------------------

cmd.echo('</script_fof.lua>')
