local ev_error_handlers

_println = assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end
_eprintln = assert(_eprint)
eprintln = function (fmt, ...) return _eprint(string.format(fmt, ...)) end
print = function (...)
	local t = {...}
	local l = select('#', ...)
	if l > 0 then
		for i = 1, l do
			t[i] = tostring(t[i])
		end
		return println('%s', table.concat(t, '\t'))
	end
end

fatal = assert(_fatal)

setmetatable(_G, {
	__index = function (self, k)
		if k == 'panic' then
			assert(coroutine.running(), 'tried to get panic function outside a coroutine')
			return ev_error_handlers[coroutine.running()]
		end
		return error('tried to access nonexistent variable ' .. tostring(k), 2)
	end,
	__newindex = function (self, k, v)
		if k == 'panic' then
			assert(coroutine.running(), 'tried to set panic function outside a coroutine')
			assert(v == nil or type(v) == 'function', 'panic function must be a function')
			ev_error_handlers[coroutine.running()] = v
			return
		end
		return rawset(self, k, v)
	end,
})

assert((type(agpl_source_url) == 'string' and #agpl_source_url > 0), 'invalid agpl_source_url')
println('cfgfs is free software released under the terms of the GNU AGPLv3 license.')
println('Type `cfgfs_license\' for details.')

do
	local author, repo = agpl_source_url:match('^git@github.com:([A-Za-z0-9_]+)/([A-Za-z0-9_]+)%.git$')
	if author then
		agpl_source_url = string.format('https://github.com/%s/%s', author, repo)
	end
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
		init_on_reload = true,

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

local ev_timeouts = make_resetable_table()

ev_error_handlers = setmetatable({}, {__mode = 'k'}) -- local

-- callback to call next time the coroutine yields or returns
local ev_return_handlers = setmetatable({}, {__mode = 'k'})

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

do
	local ev_loop_fn_main = function (...)
		local args = {...}
		local this_co = coroutine.running()
		while true do
			args[1](select(2, table.unpack(args)))
			if ev_loop_co ~= this_co then
				break
			end
			ev_error_handlers[this_co] = nil
			ev_return_handlers[this_co] = nil
			args = {coroutine.yield(sym_ready)}
		end
	end
	local ev_loop_fn_catch = function (err)
		return err, debug.traceback(err, 2)
	end
	ev_loop_fn = function (...)
		return xpcall(ev_loop_fn_main, ev_loop_fn_catch, ...)
	end
end
ev_loop_co = coroutine.create(ev_loop_fn)

local ev_handle_return = function (co, ok, rv1, rv2, rv3)
	if ok then
		if co == ev_loop_co and rv1 ~= sym_ready then
			ev_loop_co = coroutine.create(ev_loop_fn)
		end
		if rv1 == false and coroutine.status(co) == 'dead' then
			local handler = ev_error_handlers[co]
			if handler then
				ev_error_handlers[co] = nil
				ev_call(handler, rv2)
			else
				eprintln('\aerror: %s', rv3)
			end
			local ok, err = coroutine.close(co) -- hope this doesn't yield?
			if not ok then
				eprintln('\aerror: %s', err)
			end
		end
		local cb = ev_return_handlers[co] -- badly named
		if cb then
			ev_return_handlers[co] = nil
			return ev_call(cb, co, cb)
		end
	else
		local ok, err = coroutine.close(co)
		if not ok then
			eprintln('\aerror: %s', err)
		end
		-- idiot forgot to use pcall
		return error(rv1)
	end
end

ev_call = function (fn, ...) -- local
	ev_outoftheway()
	return ev_handle_return(ev_loop_co, coroutine.resume(ev_loop_co, fn, ...))
end
local ev_resume = function (co, ...)
	return ev_handle_return(co, coroutine.resume(co, ...))
end

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
	-- if this thing ever gets the ability to cancel timeouts, then grabbing
	--  and replacing the global list like this won't do because there needs
	--  to be a way to remove a timeout from it from outside
	-- possibly better alternative: put timeouts in a sorted list and in
	--  the loop here, check the item at the head and only remove it if we're
	--  going to run it
	-- ^ may need something to avoid resuming wait(0) ones too early (like a
	--  "generation" counter, don't resume ones added during this same cfgfs
	--  call)

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

spinoff = ev_call

local resume_cos = {}
local resume_id = 0

reexec = function ()
	local id = tostring(resume_id)
	resume_id = resume_id+1
	resume_cos[id] = assert(coroutine.running())
	cfgf('exec"cfgfs/resume/%d', id)
	return coroutine.yield()
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
setmetatable(is_pressed, {
	__index = function (_, key)
		return error(string.format('unknown key "%s"', key), 2)
	end,
})

--------------------------------------------------------------------------------

-- cfg/cmd magic tables

cfg = assert(_cfg)
cfgf = function (fmt, ...) return cfg(string.format(fmt, ...)) end

-- note: this is a separate table so that reassigning a value always calls __newindex()
local cmd_fns = make_resetable_table()
cmd = setmetatable({--[[ empty ]]}, {
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
				v = tostring(v)
				cmd_fns[k] = function () return cfg(v) end
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
				v = tostring(v)
				cmd_fns[k] = function () return cfg(v) end
			else
				cmd_fns[k] = nil
			end
		end
	end,
	__call = assert(_cmd),
})
local cmdv_fns = make_resetable_table()
cmdv = setmetatable({--[[ empty ]]}, {
	__index = function (_, k)
		local fn = cmdv_fns[k]
		if fn then
			return fn
		end
		fn = function (...) return cmdv(k, ...) end
		cmdv_fns[k] = fn
		return fn
	end,
	__newindex = function (_, k, v)
		cmd[k] = v
	end,
	__call = function (_, ...)
		cmd('echo', '+', ...)
		return cmd(...)
	end,
})
local cmdp_fns = make_resetable_table()
cmdp = setmetatable({--[[ empty ]]}, {
	__index = function (_, k)
		local fn = cmdp_fns[k]
		if fn then
			return fn
		end
		fn = function (...) return cmdp(k, ...) end
		cmdp_fns[k] = fn
		return fn
	end,
	__newindex = function (_, k, v)
		cmd[k] = v
	end,
	__call = function (_, ...)
		return cmd('echo', '+', ...)
	end,
})

-- works like 99% of the time
run_capturing_output = function (cmd)
	reexec()

	local lines = {}
	local fn = function (line)
		return table.insert(lines, line)
	end
	add_listener('game_console_output', fn)

	if type(cmd) == 'function' then
		ev_call(cmd)
	else
		cfg(cmd)
	end

	reexec()

	remove_listener('game_console_output', fn)

	return lines
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

cvar = setmetatable({--[[ empty ]]}, {
	__index = function (_, k)
		return get_cvars({k})[k]
	end,
	__newindex = function (_, k, v)
		-- ignore nil i guess
		if v ~= nil then
			return cmd(k, v)
		end
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
		return error(string.format('tried to bind unknown key "%s"', key), 2)
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
	if t then
		table.insert(t, cb)
		t[cb] = true
	else
		events[name] = {[1] = cb, [cb] = true}
	end
end

remove_listener = function (name, cb)
	if type(name) == 'table' then
		for _, name in ipairs(name) do
			remove_listener(name, cb)
		end
		return
	end
	local t = events[name]
	if t and t[cb] then
		if #t > 1 then
			for i = 1, #t do
				if t[i] == cb then
					table.remove(t, i)
					t[cb] = nil
					break
				end
			end
		else
			assert(#t == 1)
			t[cb] = nil
			events[name] = nil
		end
	end
end

local last_event = nil

fire_event = function (name, ...)
	local t = events[name]
	if t then
		local copy = {table.unpack(t)}
		for _, cb in ipairs(copy) do
			if t[cb] then
				last_event = name
				if type(cb) == 'function' then
					ev_call(cb, ...)
				elseif type(cb) == 'thread' then
					ev_resume(cb, ...)
				end
			end
		end
	end
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
	local t = {coroutine.yield()}
	done = true
	remove_listener(name, this_co)
	return last_event, table.unpack(t)
end

-- like wait_for_event() but usable in a for-in loop
-- the timeout is for the whole thing rather than one event
-- ^ unless timeout_per_event is true
wait_for_events = function (name, timeout, timeout_per_event)
	if timeout then
		if not timeout_per_event then
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
				return wait_for_event(name, timeout)
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
	_in_get_contents = true
	_get_contents_(path)
	_in_get_contents = false
end
_in_get_contents = false

_get_contents_ = function (path)
	-- keybind?
	local t = bindfilenames[path]
	if t then
		local name = t.name
		if t.type == 'down' then

			is_pressed[name] = _ms()
			local v = binds_down[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, true, name)
				else
					cfg(v)
				end
			end
			return fire_event('+'..name, true, name)

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
			return fire_event('-'..name, false, name)

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
				return fire_event('-'..name, false, name)

			else

				is_pressed[name] = _ms()
				local v = binds_down[name]
				if v then
					if type(v) == 'function' then
						ev_call(v, true, name)
					else
						cfg(v)
					end
				end
				return fire_event('+'..name, true, name)

			end
		elseif t.type == 'once' then

			is_pressed[name] = _ms()
			local v = binds_down[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, true, name)
				else
					cfg(v)
				end
			end
			fire_event('+'..name, true, name)

			is_pressed[name] = false
			local v = binds_up[name]
			if v then
				if type(v) == 'function' then
					ev_call(v, false, name)
				else
					cfg(v)
				end
			end
			return fire_event('-'..name, false, name)

		else
			-- fatal
			return error('unknown bind type')
		end
		-- ^ this is duplicated because function calls are SLOW ^
		return
	end
	if path == '/cfgfs/click.cfg' then
		-- new: only do timeouts specifically on click
		-- this way timeouts and key events can't happen in the wrong order
		_click_received()
		return ev_do_timeouts()
	end
	if path == '/cfgfs/buffer.cfg' then
		return
	end

	path = path:after('/')
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
				return ev_call(f, select(2, table.unpack(t)))
			else
				return eprintln('warning: tried to exec nonexistent alias %s', m)
			end
		end

		local m = path:between('resume/', '.cfg')
		if m then
			local f = resume_cos[m]
			if f then
				resume_cos[m] = nil
				return ev_resume(f)
			else
				return eprintln('warning: tried to resume nonexistent coroutine %s', m)
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
			fire_event('-'..key, false, key)
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

-- ...
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

local lua_mode = true
if lua_mode then _set_prompt('> ') end

local attention_message_shown = false
local yield_id = 1

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
	if not lua_mode and line:find('^!') then
		line = line:sub(2)
		do_lua = true
	end
	do_lua = (do_lua or lua_mode)

	local buffer_was_empty = _buffer_is_empty()

	if do_lua then
		local id = yield_id
		local cb_initial
		local cb_yielded
		cb_initial = function (co, cb)
			if co ~= ev_loop_co and coroutine.status(co) == 'suspended' then
				eprintln('\27[1;34m->\27[0m %d', id)
				yield_id = (yield_id + 1)
				ev_return_handlers[co] = cb_yielded

				if  not is_game_window_active()
				and not attention_message_shown
				then
					println('note: execution won\'t be resumed until the game window is activated')
					attention_message_shown = true
				end
			end
		end
		cb_yielded = function (co, cb)
			if coroutine.status(co) == 'dead' then
				eprintln('\27[1;34m<-\27[0m %d', id)
			else
				ev_return_handlers[co] = cb_yielded
			end
		end
		ev_return_handlers[ev_loop_co] = cb_initial
		local fn, err = repl_fn(line)
		if not fn then
			eprintln('%s', err)
			return
		end
		ev_call(fn)
	else
		cfg(line)
	end

	if  buffer_was_empty
	and not _buffer_is_empty()
	and not is_game_window_active()
	and not attention_message_shown
	then
		println('note: commands won\'t be executed until the game window is activated')
		attention_message_shown = true
	end

end

local our_logfile = assert(io.open('console.log', 'a'))
our_logfile:setvbuf('line')

_game_console_output = function (line)
	-- detect some useless error messages
	local spam = false
	if #line >= 19 then
		local c = line:sub(1, 1)
		if c == '-' and #line >= 27 and line:find('^%-%-%- Missing Vgui material ') then goto match end
		if c == 'A' and #line >= 48 and line:find('^Attemped to precache unknown particle system ".*"!$') then goto match end
		if c == 'C' and #line >= 44 and line:find('^Cannot update control point [0-9]+ for effect \'.*\'%.$') then goto match end
		if c == 'E' and #line >= 34 and line:find('^EmitSound: pitch out of bounds = %-?[0-9]+$') then goto match end
		if c == 'E' and #line >= 41 and line:find('^Error: Material ".*" uses unknown shader ".*"$') then goto match end
		if c == 'F' and #line >= 102 and line:find('^Failed to find attachment point specified for .* event%. Trying to spawn effect \'.*\' on attachment named \'.*\'$') then goto match end
		if c == 'M' and #line >= 68 and line:find('^Model \'.*\' doesn\'t have attachment \'.*\' to attach particle system \'.*\' to%.$') then goto match end
		if c == 'N' and #line >= 35 and line:find('^No such variable ".*" for material ".*"$') then goto match end
		if c == 'R' and #line >= 78 and line:find('^Requesting texture value from var ".*" which is not a texture value %(material: .*%)$') then goto match end
		if c == 'S' and #line >= 36 and line:find('^Shutdown function [A-Za-z0-9_]+%(%) not in list!!!$') then goto match end
		if c == 'S' and #line >= 49 and line:find('^SetupBones: invalid bone array size %([0-9]+ %- needs [0-9]+%)$') then goto match end
		if c == 'U' and #line >= 19 and line:find('^Unable to remove .+!$') then goto match end
		if c == 'm' and #line >= 42 and line:find('^m_face%->glyph%->bitmap%.width is [0-9]+ for ch:[0-9]+ ') then goto match end
		if line == 'Unknown command "dimmer_clicked"' then goto match end
		goto nomatch
		::match::
		spam = true
		::nomatch::
	end

	-- print the line to the terminal
	-- color codes: /usr/share/doc/xterm-*/ctlseqs.txt (ctrl+f "Character Attributes")
	if not spam then
		-- copy the line so we can modify it
		local line = line

		-- colorize color codes so the \7 doesn't cause the terminal to blink
		-- line="\7FFD700[RTD]\1 Rolled \00732CD32Lucky Sandvich\1."
		if line:byte(1) == 7 then
			--line = line:gsub('\0[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]([^\1]*)\1', '%1')
			-- ^ where did this come from? i thought it couldn't print null bytes
			line = line:gsub('\7([0-9A-F][0-9A-F])([0-9A-F][0-9A-F])([0-9A-F][0-9A-F])([^\1]*)\1', function (r, g, b, s)
				r, g, b = tonumber('0x'..r), tonumber('0x'..g), tonumber('0x'..b)
				return string.format('\27[38;2;%d;%d;%dm%s\27[0m', r, g, b, s)
			end)
		end

		-- clean up newline spam
		if line:find('\n', 1, true) then
			line = line:gsub('\n[\n ]*', '\n'):gsub('^\n+', '', 1):gsub('\n+$', '', 1)
		end

		-- make crit kills blink
		line = line:gsub(' %(crit%)$', ' \27[1;5;37;41m(crit)\27[0m', 1)

		_println(line)
	else
		-- 2 = faint
		println('\27[2m%s\27[0m', line)
	end

	our_logfile:write(line, '\n')
	fire_event('game_console_output', line)

	-- agpl backdoor (https://www.gnu.org/licenses/gpl-howto.en.html)
	-- it doesn't work if you say it yourself
	if line:find(':[\t ]*!cfgfs_agpl_source[\t ]*$') then
		cmd.say(agpl_source_url)
	end
end

--------------------------------------------------------------------------------

_control = function (line)
	return fire_event('control_message', line)
end

--------------------------------------------------------------------------------

local logpos = 0
do
	local f <close> = assert(io.open('console.log', 'r'))
	logpos = assert(f:seek('end'))
end

open_log = function ()
	local f = assert(io.open('console.log', 'r'))
	assert(f:seek('set', logpos))
	return f
end

grep = function (pat)
	local f <close> = open_log()
	for line in f:lines() do
		if line:find(pat) then
			println('%s', line)
		end
	end
end

--------------------------------------------------------------------------------

local builtin_aliases = function ()
	cmd.cfgfs_license = 'exec cfgfs/license'
	cmd.cfgfs_source = function () return cmd.echo(agpl_source_url) end
end

-- reload and re-run script.lua
_reload_1 = function ()
	local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or './script.lua')
	if not ok then
		eprintln('\aerror: %s', err)
		eprintln('failed to reload script.lua!')
		return false
	end
	local script = ok

	_fire_unload(false)
	do_reset()
	local ok, err = xpcall(script, debug.traceback)
	builtin_aliases()
	if not ok then
		eprintln('\aerror: %s', err)
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

-- https://wikidiff.com/exit/quit
-- As nouns the difference between exit and quit is that exit is a way out
--  while quit is any of numerous species of small passerine birds native
--  to tropical america.
-- As verbs the difference between exit and quit is that exit is to go out
--  while quit is to pay (a debt, fine etc).
_fire_unload = function (exiting)
	return fire_event('unload', exiting)
end

--------------------------------------------------------------------------------

local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or './script.lua')
if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end
local script = ok

local ok, err = xpcall(script, debug.traceback)
builtin_aliases()
if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end

--------------------------------------------------------------------------------

collectgarbage()

return true
