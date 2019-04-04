math.randomseed(os.time())

local outf = function (fmt, ...)
	return coroutine.yield(string.format(fmt, ...))
end

local classes = {
	'scout',
	'soldier',
	'pyro',
	'demoman',
	'heavyweapons',
	'engineer',
	'medic',
	'sniper',
	'spy',
}

local cfg = {
	['autoexec.cfg'] = function ()
		cfg['autoexec.cfg'] = nil
		outf('exec autoexec')
		for key in pairs(cfg.binds) do
			local _, _, key = key:find('^%+(.*)%.cfg$')
			if key then
				outf('alias +%s "exec binds/+%s"', key, key)
				outf('alias -%s "exec binds/-%s"', key, key)
				outf('bind %s +%s', key, key)
			end
		end
	end,
	binds = {},
	test = 'hi\n',
}

local bind = function (key, command, up_command)
	local binds = cfg.binds
	if type(command) == 'string' and up_command == nil and command:find('^+') then
		up_command = '-' .. command:sub(2)
	end
	binds['+' .. key .. '.cfg'] =
		type(command) == 'string' and command .. '\n' or binds['+' .. key .. '.cfg'] or ''
	binds['-' .. key .. '.cfg'] =
		type(up_command) == 'string' and up_command .. '\n' or binds['-' .. key .. '.cfg'] or ''
end

bind('w', '+forward')
bind('s', '+back')
bind('a', '+moveleft')
bind('d', '+moveright')

return cfg
