_println = assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end
_eprintln = assert(_eprint)
eprintln = function (fmt, ...) return _eprint(string.format(fmt, ...)) end
print = function (...)
	local t = {...}
	for i = 1, #t do
		t[i] = tostring(t[i])
	end
	return println('%s', table.concat(t, '\t'))
end

setmetatable(_G, {
	__index = function (self, k)
		eprintln('warning: tried to access nonexistent variable %s', tostring(k))
		eprintln('%s', debug.traceback(nil, 2))
	end,
})

println('cfgfs is free software released under the terms of the GNU AGPLv3 license.')
println('Type `cfgfs_license\' for details.')

--------------------------------------------------------------------------------

local gd, gn = mountpoint:match('^(.+/steamapps/common/([^/]+)/[^/]+)/custom/[^/]+/cfg$')
if gd then
	println('recognized game: %s', gn)
	gamedir = gd
else
	println('note: mounted outside game directory')
	gamedir = false
end

--------------------------------------------------------------------------------

-- helper for defining globals in script.lua that persist across reloads
-- usage:
--   varname = global('varname', 'default value if it wasn\'t defined before')
--   varname = global('varname', { _v=1, key=value })
-- to redefine a table that already exists, change its "_v" key to a different value

global = function (k, v2)
	local v1 = rawget(_G, k)
	if v1 ~= nil and not (type(v1) == 'table' and type(v2) == 'table' and v1._v ~= v2._v) then
		return v1
	end
	rawset(_G, k, v2)
	return v2
end

--------------------------------------------------------------------------------

-- machinery to reset all state for reloading script.lua

local reset_callbacks = {}

local add_reset_callback = function (fn)
	return table.insert(reset_callbacks, fn)
end

local make_resetable_table = function (init)
	local t = {}
	add_reset_callback(function ()
		for k in pairs(t) do
			rawset(t, k, nil)
		end
	end)
	return t
end

local do_reset = function ()
	for _, fn in ipairs(reset_callbacks) do
		fn()
	end
end

--------------------------------------------------------------------------------

-- crappy event loop thing

local sym_ok = {}
local sym_wait = {}

local ev_loop_co
local ev_loop_fn
ev_loop_fn = function (...)
	local t = {...}
	local ok, rv = xpcall(function ()
		local t = t
		while true do
			t[1](select(2, table.unpack(t)))
			if ev_loop_co ~= coroutine.running() then
				break
			end
			t = {coroutine.yield(sym_ok)}
		end
	end, debug.traceback)
	if not ok then
		eprintln('error: %s', rv)
	end
	ev_loop_co = coroutine.create(ev_loop_fn)
end
ev_loop_co = coroutine.create(ev_loop_fn)

local ev_handle_return = function (co, ok, rv1)
	if not ok then
		-- idiot forgot to use pcall
		return error(rv1)
	end
end

local ev_call = function (fn, ...)
	if coroutine.running() == ev_loop_co then
		ev_loop_co = coroutine.create(ev_loop_fn)
	end
	return ev_handle_return(ev_loop_co, coroutine.resume(ev_loop_co, fn, ...))
end
local ev_resume = function (co, ...)
	return ev_handle_return(co, coroutine.resume(co, ...))
end

local ev_timeouts = make_resetable_table()

local ev_wait = function (ms)
	local target = 0
	if ms > 0 then
		target = _ms()+ms
		_click_at(target)
	else
		_click()
	end
	if coroutine.running() == ev_loop_co then
		ev_loop_co = coroutine.create(ev_loop_fn)
	end
	table.insert(ev_timeouts, {
		target = target,
		co = assert(coroutine.running()),
	})
	return coroutine.yield(sym_wait)
end

local ev_do_timeouts = function ()
	local ts = ev_timeouts
	if #ts == 0 then return end

	local newts = {}
	ev_timeouts = {}

	local now = nil
	for i = 1, #ts do
		now = now or _ms()
		local t = ts[i]
		if now >= t.target then
			ev_resume(t.co)
			now = nil
		else
			table.insert(newts, t)
		end
	end
	if #newts > 0 then
		if #ev_timeouts > 0 then -- new ones were added
			for _, t in ipairs(ev_timeouts) do
				table.insert(newts, t)
			end
		end
		ev_timeouts = newts
	end
end

local ev_spinoff = function (fn)
	return ev_call(function ()
		local ok, rv = xpcall(function ()
			return fn(v)
		end, debug.traceback)
		if not ok then
			eprintln('error: %s', rv)
		end
	end)
end

-- i think these worked
set_timeout = function (fn, ms)
	local cancelled = false
	spinoff(function ()
		wait(ms)
		if cancelled then return end
		return fn()
	end)
	return {
		cancel = function ()
			cancelled = true
		end,
	}
end
set_interval = function (fn, ms)
	local cancelled = false
	spinoff(function ()
		while true do
			wait(ms)
			if cancelled then break end
			fn()
		end
	end)
	return {
		cancel = function ()
			cancelled = true
		end,
	}
end

wait = ev_wait
spinoff = ev_spinoff

--------------------------------------------------------------------------------

-- settings table

do

local init_settings = function ()
	cfgfs = {
		-- run fuse in single-threaded mode
		-- ~10% faster but can cause lockups if something in script.lua accesses the cfgfs mount
		-- (the request can't be processed until the script returns)
		fuse_singlethread = true,

		-- output the "initial output" of script.lua when it's reloaded
		-- (should only disable if you're testing things manually and it gets in the way)
		init_on_reload = (gamedir) and true or false,

		-- output the "initial output" of script.lua each time after any of these these configs are executed
		-- probably should be something that's always executed late during the game startup process
		init_after_cfg = {
			-- ['example.cfg'] = true,
		},

		-- same as init_after_cfg but before the config
		-- (maybe not very useful?)
		init_before_cfg = {
			-- ['example.cfg'] = true,
		},

		-- avoid interfering with the execution these "configs"
		-- add files from the cfg directory that aren't in cfg format here
		-- (messing with them causes problems)
		intercept_blacklist = {
			-- tf2
			['motd_entries.txt'] = true,
			['mtp.cfg'] = true,
			['server_blacklist.txt'] = true,
			-- fof
			['banned_ip.cfg'] = true,
			['banned_user.cfg'] = true,
			['pure_server_full.txt'] = true,
			['server_blacklist.txt'] = true,
			['settings.scr'] = true,
			['user.scr'] = true,
		},

		-- prevent the game from executing these configs
		intercept_blackhole = {
			-- ['example.cfg'] = true,
		},

		-- after script.lua is reloaded, "restore" these globals by firing the corresponding events with their values
		-- (globals normally persist across reloads though)
		restore_globals_on_reload = {
			['class'] = 'classchange',
			['slot'] = 'slotchange',
		},

		-- work around lack of alias command for games that don't have it (fof)
		compat_noalias = false,
	}
end

init_settings()
add_reset_callback(init_settings)

end

--------------------------------------------------------------------------------

local aliases = make_resetable_table()

local binds_down = make_resetable_table()
local binds_up = make_resetable_table()

local events = make_resetable_table()

unmask_next = make_resetable_table()
is_pressed = make_resetable_table()

--------------------------------------------------------------------------------

-- cfg/cmd magic tables

cfg = assert(_cfg)

cfgf = function (fmt, ...) return cfg(string.format(fmt, ...)) end

cmd = setmetatable({}, {
	__index = function (self, k)
		local fn = function (...) return cmd(k, ...) end
		rawset(self, k, fn)
		return fn
	end,
	__newindex = function (self, k, v)
		if cfgfs.compat_noalias then
			if type(v) ~= 'function' then
				local s = v
				v = function () return cfg(s) end
			end
			return rawset(self, k, v)
		end
		if v == nil then
			rawset(self, k, nil)
			return cmd.alias(k, '')
			-- ^ there's no "unalias" command so just set it to empty
		end
		if type(v) == 'function' then
			rawset(self, k, v)
			return cmd.alias(k, 'exec', 'cfgfs/alias/'..k)
		end
		v = tostring(v)
		rawset(self, k, function () return cfg(v) end)
		return cmd.alias(k, v)
	end,
	__call = assert(_cmd),
})

run_capturing_output = function (cmd)
	local f <close> = assert(io.open((gamedir or '.') .. '/console.log', 'r'))
	f:seek('end')
	if type(cmd) ~= 'function' then
		cfg(cmd)
	else
		cmd()
	end
	wait(0)
	local t = {}
	for l in f:lines() do
		table.insert(t, l)
	end
	return t
end

local get_cvars = function (t)
	local rv = {}
	for _, l in ipairs(run_capturing_output(function ()
		for _, k in ipairs(t) do
			cmd.help(k)
		end
	end)) do
		local mk, mv = l:match('"([^"]+)" = "([^"]*)"')
		if mk then
			rv[mk] = mv
		end
	end
	return rv
end
-- note: the match pattern does not use "^"
-- due to mysteries, the console output sometimes comes up jumbled with the
--  order of words messed up
-- like
--[[
	] help con_enable
	 archive"con_enable" = "1"
	 ( def. "0" ) - Allows the console to be activated.
	] help con_enable
	 - Allows the console to be activated.
	"con_enable" = "1" ( def. "0" )
	 archive
	] help con_enable
	 ( def. "0" )"con_enable" = "1"
	 archive
	 - Allows the console to be activated.
]]
-- etc etc
-- i wonder what causes it
-- luckily the `"name" = "value"` part is always intact

cvar = setmetatable({}, {
	__index = function (_, k)
		return get_cvars({k})[k]
	end,
	__newindex = function (_, k, v)
		-- ignore nil i guess
		if v == nil then return end
		return cmd(k, v)
	end,
	-- calling this should get multiple cvars in one go but i'm not sure how it should be used
	-- like should t be like a list or a map with default values
	-- aa
	-- and doesn't calling this look more like it's going to set them instead of getting
	-- especially with default values it would
	-- even if this could set multiple at once there would be no advantage over doing them separately
	-- but what matters more is what it looks like it's doing
	__call = function (_, t)
		return error('not supported')
	end,
})

--------------------------------------------------------------------------------

bind = function (key, cmd, cmd2)
	local n = key2num[key]
	if not n then
		eprintln('warning: tried to bind unknown key "%s"', key)
		eprintln('%s', debug.traceback(nil, 2))
		return
	end
	if not cfgfs.compat_noalias then
		if not binds_down[key] then
			local cmd = _G.cmd
			cmd.alias('+key'..n, 'exec', 'cfgfs/keys/+'..n)
			cmd.alias('-key'..n, 'exec', 'cfgfs/keys/-'..n)
			cmd.bind(key, '+key'..n)
		end
	else
		local cmd = _G.cmd
		if cmd2 then
			cmd.bind(key, '+jlook;exec cfgfs/keys/^'..n..'//')
		else
			cmd.bind(key, 'exec cfgfs/keys/@'..n)
		end
		-- (if cmd2 then ...):
		-- the bind string needs to start with + to run something on key release too
		-- +jlook is there because it does nothing and starts with +
		-- "//" at the end starts a comment to ignore excess arguments
	end
	-- add -release command for +toggles
	if not cmd2 and type(cmd) ~= 'function' then
		cmd = (tostring(cmd) or '')
		if cmd:find('^%+') then
			cmd2 = ('-' .. cmd:sub(2))
		end
	end
	binds_down[key] = cmd
	binds_up[key] = cmd2
end
-- at some point, should try making this bind simple string commands directly
-- (so that they don't call into cfgfs)
-- add_key_listener() needs to work though
-- * check here if the key has listeners -> don't bind directly
-- * check in add_key_listener() if it didn't have listeners before -> make exec
-- * check in remove_key_listeners() if it was the last one -> make direct
-- my most pressed keys are already functions though
-- ** this would help with commands that need a key bound to them directly
-- ** calling into cfgfs for everything helped(?) do timeouts when click() didn't exist
-- *** fug what do i do now that add_key_listener() is gone
--     -> reinstate but use the new events internally

--------------------------------------------------------------------------------

local num2class = {
	'scout', 'soldier', 'pyro',
	'demoman', 'heavyweapons', 'engineer',
	'medic', 'sniper', 'spy',
}

local class2num = {}

for num, class in ipairs(num2class) do
	class2num[class] = num
end

--------------------------------------------------------------------------------

-- crappy event system
-- now slightly less crappy

add_listener = function (name, cb)
	if type(name) == 'table' then
		for _, name in ipairs(name) do
			add_listener(name, cb)
		end
		return
	end
	events[name] = events[name] or {}
	table.insert(events[name], cb)
end

fire_event = function (name, ...)
	local t = events[name]
	if not t then return end
	for _, cb in ipairs(t) do
		ev_call(cb, ...)
	end
end

--------------------------------------------------------------------------------

_get_contents = function (path)
	ev_do_timeouts()

	-- keybind?
	local t = bindfilenames[path]
	if t then
		local name = t.name
		if t.type == 'down' then

			is_pressed[name] = true
			local v = binds_down[name]
			if v then
				if type(v) ~= 'function' then
					cfg(v)
				else
					ev_call(v)
				end
			end
			return fire_event('+'..name)

		elseif t.type == 'up' then

			is_pressed[name] = false
			local v = binds_up[name]
			if v then
				if type(v) ~= 'function' then
					cfg(v)
				else
					ev_call(v)
				end
			end
			return fire_event('-'..name)

		elseif t.type == 'toggle' then
			if is_pressed[name] then

				is_pressed[name] = false
				local v = binds_up[name]
				if v then
					if type(v) ~= 'function' then
						cfg(v)
					else
						ev_call(v)
					end
				end
				return fire_event('-'..name)

			else

				is_pressed[name] = true
				local v = binds_down[name]
				if v then
					if type(v) ~= 'function' then
						cfg(v)
					else
						ev_call(v)
					end
				end
				return fire_event('+'..name)

			end
		elseif t.type == 'once' then

			is_pressed[name] = true
			local v = binds_down[name]
			if v then
				if type(v) ~= 'function' then
					cfg(v)
				else
					ev_call(v)
				end
			end
			fire_event('+'..name)

			is_pressed[name] = false
			local v = binds_up[name]
			if v then
				if type(v) ~= 'function' then
					cfg(v)
				else
					ev_call(v)
				end
			end
			return fire_event('-'..name)

		else
			return error('unknown bind type')
		end
		-- ^ this is duplicated because function calls are SLOW ^
		return
	end
	if path == '/cfgfs/buffer.cfg' then
		return
	end

	path = assert(path:after('/'))
	local m = path:after('cfgfs/')
	if m then
		path = m

		local m = path:between('alias/', '.cfg')
		if m then
			local t = {}
			for arg in m:gmatch('[^/]+') do
				table.insert(t, arg)
			end
			local f = rawget(cmd, t[1])
			if f then
				if type(f) ~= 'function' then
					return cfg(v)
				else
					return ev_call(f, select(2, table.unpack(t)))
				end
			else
				return eprintln('warning: tried to exec nonexistent alias %s', m)
			end
		end

-- next (number) times cfgfs_getattr() gets a request about this file, say it doesn't exist
		local m = path:after('unmask_next/')
		if m then
			local newval = 3
--			eprintln('unmask_next[%s]: %d -> %d (lua)', m, unmask_next[m] or 0, newval)
			unmask_next[m] = newval
			return
		end

		if path == 'license.cfg' then
			local f <close> = assert(io.open('LICENSE', 'r'))
			for line in f:lines() do
				cmd.echo(line)
			end
			return
		end

		if path == 'init.cfg' then
			return _init()
		end

		return eprintln('warning: unknown cfgfs config "%s"', path)
	else
		local m = path:before('.cfg') or path

		if m == 'config' then
			cmd.echo('')
			cmd.echo('cfgfs is free software released under the terms of the GNU AGPLv3 license.')
			cmd.echo('Type `cfgfs_license\' for details.')
			cmd.echo('')
		end

		if cfgfs.init_before_cfg[path] then
			_init()
		end

		if not cfgfs.intercept_blackhole[path] then
			cfgf('exec"cfgfs/unmask_next/%s.cfg";exec"%s.cfg"', m, m)
		end

		if cfgfs.init_after_cfg[path] then
			_init()
		end

		if class2num[m] then
			fire_event('classchange', m)
		end
	end
end

-- relief for buggy toggle keys
release_all_keys = function ()
	for key in pairs(is_pressed) do
		is_pressed[key] = nil
		local vu = binds_up[key]
		if vu ~= nil then
			if type(vu) ~= 'function' then
				cfg(vu)
			else
				cmd.exec(string.format('cfgfs/keys/-%d', key2num[key]))
				-- we're (probably) in the coroutine so can't resume it here, need to use exec
			end
		end
	end
end

do

local is_active = nil
local game_title = nil

_attention = function (active_title)
	if game_title == nil then
		return nil
	end
	local was_active = is_active
	is_active = (active_title == game_title)
	if is_active ~= was_active then
		fire_event('attention', is_active)
	end
	return is_active
end

is_game_window_active = function ()
	return is_active
end

local game_window_title_is = function (title)
	game_title = title
	return _update_attention(_attention(_get_attention()))
end

-- this shouldn't be a function but it is
-- until there's a way to watch for property changes on the cfgfs table
-- why can't i accept that there's no reason to ever change this after startup?
-- why does it need to be changeable?
-- because the value in the table can be changed at any time so it's natural that one would expect changing it to work
-- this sucks
cfgfs.game_window_title_is = game_window_title_is

add_reset_callback(function ()
	cfgfs.game_window_title_is = game_window_title_is
end)

end

--------------------------------------------------------------------------------

local repl_fn = function (code)
	local fn, err1 = load(code, 'input')
	if fn then
		return fn
	end
	local fn = load('return '..code, 'input')
	if fn then
		return function ()
			return print(fn())
		end
	end
	return nil, err1
end

_cli_input = function (line)
	if line:find('^!') then
		local fn, err = repl_fn(line:sub(2))
		if not fn then
			eprintln('%s', err)
			return
		end
		return ev_call(fn)
	else
		if line == 'cfgfs_license' then
			local f <close> = assert(io.open('LICENSE', 'r'))
			for line in f:lines() do
				println('%s', line)
			end
			return
		end
		return cfg(line)
	end
end

_game_console_output = function (line)
	_println(line)
	fire_event('game_console_output', line)
	-- agpl backdoor (https://www.gnu.org/licenses/gpl-howto.en.html)
	-- it doesn't work if you say it yourself
	if line:find(':[\t ]*!cfgfs_agpl_source[\t ]*$') then
		cmd.say(agpl_source_url)
	end
end
assert((type(agpl_source_url) == 'string' and #agpl_source_url > 0), 'invalid agpl_source_url')

cmd.cfgfs_license = 'exec cfgfs/license'
cmd.cfgfs_source = function () return cmd.echo(agpl_source_url) end

--------------------------------------------------------------------------------

local bell = function () return io.stderr:write('\a') end

-- reload and re-run script.lua
_reload_1 = function ()
	local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or './script.lua')
	if not ok then
		bell()
		eprintln('error: %s', err)
		eprintln('failed to reload script.lua!')
		return false
	end
	local script = ok

	do_reset()

	local ok, err = xpcall(script, debug.traceback)
	if not ok then
		bell()
		eprintln('error: %s', err)
		eprintln('script.lua was reloaded with an error!')
		return true
	end

	println('script.lua reloaded successfully')

	collectgarbage()

	return true
end

-- part 2: we re-ran script.lua and output isn't going in init.cfg anymore
_reload_2 = function (ok)
	if not ok then return end

	if cfgfs.init_on_reload then
		_init()
	end

	for name, evt in pairs(cfgfs.restore_globals_on_reload) do
		if global(name) then
			fire_event(evt, global(name))
		end
	end

	return fire_event('reload')
end

_fire_startup = function ()
	return fire_event('startup')
end

--------------------------------------------------------------------------------

local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or './script.lua')
if not ok then
	bell()
	eprintln('error: %s', err)
	eprintln('failed to load script.lua!')
	return false
end
local script = ok

local ok, err = xpcall(script, debug.traceback)
if not ok then
	bell()
	eprintln('error: %s', err)
	eprintln('failed to load script.lua!')
	return false
end

--------------------------------------------------------------------------------

collectgarbage()

return true
