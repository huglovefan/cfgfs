local ev_error_handlers

_println = assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end
_eprintln = assert(_eprint)
eprintln = function (fmt, ...) return _eprint(string.format(fmt, ...)) end
print = function (...)
	local t = {...}
	for i = 1, select('#', ...) do
		t[i] = tostring(t[i])
	end
	return println('%s', table.concat(t, '\t'))
end

setmetatable(_G, {
	__index = function (self, k)
		return error('tried to access nonexistent variable ' .. tostring(k), 2)
	end,
	__newindex = function (self, k, v)
		if k == 'panic' then
			assert(coroutine.running(), 'tried to set panic function outside a coroutine')
			assert(type(v) == 'function', 'panic function must be a function')
			ev_error_handlers[coroutine.running()] = v
			return
		end
		return rawset(self, k, v)
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

-- settings table

do

local init_settings = function ()
	cfgfs = {
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
			['mtp.cfg'] = true,
			-- fof
			['banned_ip.cfg'] = true,
			['banned_user.cfg'] = true,
		},
		-- KeyValues Error: LoadFromBuffer: missing { in file cfg/pure_server_minimal.txt
		-- KeyValues Error: LoadFromBuffer: missing { in file cfg/trusted_keys_base.txt

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

-- crappy event loop thing

-- ev_loop_co is used for calling functions in a coroutine context without
--  creating a new coroutine each time for that
-- if the function yields, ev_loop_co gets replaced with a new coroutine

local sym_ready = {}
local sym_wait = {}

local ev_loop_co
local ev_loop_fn

local ev_call

local ev_outoftheway = function () -- i had a descriptive name for this before but i forgot what it was
	if coroutine.running() == ev_loop_co then
		ev_loop_co = coroutine.create(ev_loop_fn)
	end
end

ev_error_handlers = setmetatable({}, {__mode = 'k'}) -- local

ev_loop_fn = function (...)
	local args = {...}
	local this_co = coroutine.running()
	local ok, rv = xpcall(function ()
		local args = args
		local this_co = this_co
		while true do
			args[1](select(2, table.unpack(args)))
			if ev_loop_co ~= this_co then
				break
			end
			args = {coroutine.yield(sym_ready)}
		end
	end, function (err)
		local handler = ev_error_handlers[this_co]
		ev_error_handlers[this_co] = nil
		if handler then
			return ev_call(handler, err)
		else
			eprintln('error: %s', debug.traceback(err, 2))
		end
	end)
end
ev_loop_co = coroutine.create(ev_loop_fn)

local ev_handle_return = function (co, ok, rv1)
	if co == ev_loop_co and rv1 ~= sym_ready then
		ev_loop_co = coroutine.create(ev_loop_fn)
	end
	if not ok then
		-- idiot forgot to use pcall
		return error(rv1)
	end
end

ev_call = function (fn, ...) -- local
	ev_outoftheway()
	return ev_handle_return(ev_loop_co, coroutine.resume(ev_loop_co, fn, ...))
end
local ev_resume = function (co, ...)
	-- assert(co ~= coroutine.running())
	return ev_handle_return(co, coroutine.resume(co, ...))
end

local ev_timeouts = make_resetable_table()

wait = function (ms)
	local target = 0
	if ms > 0 then
		target = _ms()+ms
		_click_at(target)
	else
		_click()
	end
	table.insert(ev_timeouts, {
		target = target,
		co = coroutine.running(),
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

spinoff = function (fn)
	return ev_call(function ()
		local ok, rv = xpcall(fn, debug.traceback)
		if not ok then
			eprintln('error: %s', rv)
		end
		-- todo: this should support the panic function
	end)
end

--------------------------------------------------------------------------------

local aliases = make_resetable_table()

local binds_down = make_resetable_table()
local binds_up = make_resetable_table()

local events = make_resetable_table()

unmask_next = make_resetable_table()

is_pressed = {}
for key in pairs(key2num) do
	is_pressed[key] = false
end

--------------------------------------------------------------------------------

-- cfg/cmd magic tables

cfg = assert(_cfg)

cfgf = function (fmt, ...) return cfg(string.format(fmt, ...)) end

-- note: this is a separate table so that reassigning a value always calls __newindex()
local cmd_fns = make_resetable_table()
cmd = setmetatable({}, {
	__index = function (self, k)
		local fn = cmd_fns[k]
		if fn then
			return fn
		end
		fn = function (...) return cmd(k, ...) end
		cmd_fns[k] = fn
		return fn
	end,
	__newindex = function (self, k, v)
		if not cfgfs.compat_noalias then
			if type(v) == 'function' then
				cmd_fns[k] = v
				cmd.alias(k, 'exec', 'cfgfs/alias/'..k)
			elseif v ~= nil then
				cmd_fns[k] = tostring(v)
				cmd.alias(k, 'exec', 'cfgfs/alias/'..k)
			else
				cmd_fns[k] = nil
				cmd.alias(k, '')
				-- ^ there's no "unalias" command so just set it to empty
			end
		else
			if type(v) == 'function' then
				cmd_fns[k] = v
			elseif v ~= nil then
				cmd_fns[k] = tostring(v)
			else
				cmd_fns[k] = nil
			end
		end
	end,
	__call = assert(_cmd),
})
cmdv = setmetatable({--[[ empty ]]}, {
	__index = function (self, k)
		local fn = function (...) return cmdv(k, ...) end
		rawset(self, k, fn)
		return fn
	end,
	__newindex = function (self, k, v)
		cmd[k] = v
	end,
	__call = function (self, ...)
		cmd('echo', '+', ...)
		return cmd(...)
	end,
})

run_capturing_output = function (cmd)
	local f <close> = assert(io.open((gamedir or '.') .. '/console.log', 'r'))
	f:seek('end')
	if type(cmd) == 'function' then
		cmd()
	else
		cfg(cmd)
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
	-- cvars list -> name/value map (gets the values)
	-- name/value map -> nothing (sets the values)
	-- (untested)
	__call = function (_, t)
		local is_list = (t[1] ~= nil)
		local is_map = true -- default to true for empty tables
		for k in pairs(t) do
			is_map = false -- not empty
			if type(k) ~= 'number' then
				is_map = true
				break
			end
		end

		if is_list ~= is_map then
			if is_list then
				return get_cvars(t)
			else
				for name, value in pairs(t) do
					cvar[name] = value
				end
				return
			end
		else
			if is_list then
				return error('ambiguous table passed to cvar', 2)
			else
				return {} -- t was empty
			end
		end
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
-- * would hide the problem with commands that need a key bound to them directly
-- * excess calls were preferred when click() didn't exist but that time is gone

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

add_listener = function (name, cb)
	if type(name) == 'table' then
		for _, name in ipairs(name) do
			add_listener(name, cb)
		end
		return
	end
	local t = events[name]
	if not t then
		t = {}
		events[name] = t
	else
		if t[cb] then return end
	end
	table.insert(t, cb)
	t[cb] = true
end

remove_listener = function (name, cb)
	if type(name) == 'table' then
		for _, name in ipairs(name) do
			remove_listener(name, cb)
		end
		return
	end
	local t = events[name]
	if not (t and t[cb]) then return end
	if #t == 1 then
		events[name] = nil
		return
	end
	for i = 1, #t do
		if t[i] == cb then
			table.remove(t, i)
		end
	end
end

local last_event = nil

fire_event = function (name, ...)
	local t = events[name]
	if not t then return end
	for _, cb in ipairs(t) do
		if t[cb] then
			last_event = name
			if type(cb) == 'function' then
				ev_call(cb, ...)
			elseif type(cb) == 'thread' then
				ev_resume(cb, ...)
			end
		end
	end
	-- it is unclear what happens if the list is modified during this loop
end

wait_for_event = function (name, timeout)
	local this_co = coroutine.running()
	local done = false
	if timeout then
		spinoff(function ()
			wait(timeout)
			if not done then
				last_event = nil
				return ev_resume(this_co)
			end
		end)
	end
	add_listener(name, this_co)
	local rv = coroutine.yield()
	done = true
	remove_listener(name, this_co)
	return last_event, rv
end

-- like wait_for_event() but usable in a for-in loop
-- the timeout is for the whole thing rather than one event
wait_for_events = function (name, timeout)
	if timeout then
		local this_co = coroutine.running()
		local timeout_done = false
		local want_resume = false
		spinoff(function ()
			wait(timeout)
			timeout_done = true
			if want_resume then
				last_event = nil
				return ev_resume(this_co)
			end
		end)
		return function ()
			if not timeout_done then
				add_listener(name, this_co)
				want_resume = true
				local rv = coroutine.yield()
				want_resume = false
				remove_listener(name, this_co)
				return last_event, rv
			else
				return nil
			end
		end
	else
		return function ()
			return wait_for_event(name)
		end
	end
end

--------------------------------------------------------------------------------

_get_contents = function (path)
	-- keybind?
	local t = bindfilenames[path]
	if t then
		local name = t.name
		if t.type == 'down' then

			is_pressed[name] = true
			local v = binds_down[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, true, name)
				else
					cfg(v)
				end
			end
			return fire_event('+'..name)

		elseif t.type == 'up' then

			is_pressed[name] = false
			local v = binds_up[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, false, name)
				else
					cfg(v)
				end
			end
			return fire_event('-'..name)

		elseif t.type == 'toggle' then
			if is_pressed[name] then

				is_pressed[name] = false
				local v = binds_up[name]
				if v then
					if type(v) == 'function' then
						ev_call(v, false, name)
					else
						cfg(v)
					end
				end
				return fire_event('-'..name)

			else

				is_pressed[name] = true
				local v = binds_down[name]
				if v then
					if type(v) == 'function' then
						ev_call(v, true, name)
					else
						cfg(v)
					end
				end
				return fire_event('+'..name)

			end
		elseif t.type == 'once' then

			is_pressed[name] = true
			local v = binds_down[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, true, name)
				else
					cfg(v)
				end
			end
			fire_event('+'..name)

			is_pressed[name] = false
			local v = binds_up[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, false, name)
				else
					cfg(v)
				end
			end
			return fire_event('-'..name)

		else
			-- fatal
			return error('unknown bind type')
		end
		-- ^ this is duplicated because function calls are SLOW ^
		return
	end
	if path == '/cfgfs/buffer.cfg' then
		-- new: only do timeouts specifically on click
		-- this way timeouts and key events can't happen in the wrong order
		return ev_do_timeouts()
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
			local f = cmd_fns[t[1]]
			if f then
				if type(f) == 'function' then
					return ev_call(f, select(2, table.unpack(t)))
				else
					return cfg(f)
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
		if path == 'config.cfg' then
			cmd.echo('')
			cmd.echo('cfgfs is free software released under the terms of the GNU AGPLv3 license.')
			if cfgfs.compat_noalias then
				cmd.echo('Type `exec cfgfs/license\' for details.')
			else
				cmd.echo('Type `cfgfs_license\' for details.')
			end
			cmd.echo('')
		end

		if cfgfs.init_before_cfg[path] then
			_init()
		end

		if not cfgfs.intercept_blackhole[path] then
			cfgf('exec"cfgfs/unmask_next/%s";exec"%s"', path, path)
		end

		if cfgfs.init_after_cfg[path] then
			_init()
		end

		local cls = (path:before('.cfg') or path)
		if class2num[cls] then
			fire_event('classchange', cls)
		end
	end
end

-- relief for buggy toggle keys
release_all_keys = function ()
	for key, pressedness in pairs(is_pressed) do
		if pressedness then
			is_pressed[key] = false
			local vu = binds_up[key]
			if vu ~= nil then
				if type(vu) == 'function' then
					ev_call(vu)
				else
					cfg(vu)
				end
			end
			fire_event('-'..key)
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
	local fn, err1 = load('return '..code, 'input')
	if fn then
		return function ()
			return print(fn())
		end
	end
	local fn = load(code, 'input')
	if fn then
		return fn
	end
	return nil, err1
end

local lua_mode = false
if lua_mode then _set_prompt('> ') end

_cli_input = function (line)

	if line == '>>' then
		eprintln('Entered Lua mode. Type "]]" to return to cfg mode.')
		lua_mode = true
		_set_prompt('> ')
		return
	end

	if line == ']]' then
		eprintln('Entered cfg mode. Type ">>" to return to Lua mode.')
		lua_mode = false
		_set_prompt('] ')
		return
	end

	if line == 'cfgfs_license' then
		local f <close> = assert(io.open('LICENSE', 'r'))
		for line in f:lines() do
			println('%s', line)
		end
		return
	end

	local do_lua = false
	if not lua_mode and fine:find('^!') then
		line = line:sub(2)
		do_lua = true
	end
	do_lua = (do_lua or lua_mode)

	if do_lua then
		local fn, err = repl_fn(line)
		if not fn then
			eprintln('%s', err)
			return
		end
		return ev_call(fn)
	else
		return cfg(line)
	end

end

--log = {}

_game_console_output = function (line)
	if line ~= '' then
		-- remove color codes
		-- mostly just want to remove the bell character so it doesn't blink my terminal
		-- line="\7FFD700[RTD]\1 Rolled \00732CD32Lucky Sandvich\1."
		if line:byte(1) < 0x20 then
			line = line:gsub('\0[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]([^\1]*)\1', '%1')
			line = line:gsub('\7[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]([^\1]*)\1', '%1')
		end
		_println(line)
	end
--	table.insert(log, {
--		time = _ms(),
--		text = line,
--	})
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
-- ^ are these in the right place? do they always get defined properly?

--------------------------------------------------------------------------------

_control = function (line)
	return fire_event('control_message', line)
end

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
