--
-- this file should really be split into modules but i haven't found a
--  good way to do that yet
-- most of the functions here depend on something else in the file so it's
--  not as simple as "copypasting one thing at a time into its own file"
-- or maybe it is if you know where to start
--

_println = assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end
_eprintln = assert(_eprint)
eprintln = function (fmt, ...) return _eprint(string.format(fmt, ...)) end

if _VERSION == 'Lua 5.1' then
	table.unpack = unpack
end

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

local ev_error_handlers

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
		intercept_cfgs = {
			['config.cfg'] = true,
		},

		hide_cfgs = {
			-- ['name.cfg'] = true,
		},

		restore_globals_on_reload = {
			-- ['varname'] = 'eventname',
		},

		compat_noalias = false,
	}

	if os.getenv('GAMENAME') == 'Team Fortress 2' then
		cfgfs.intercept_cfgs['scout.cfg'] = true
		cfgfs.intercept_cfgs['soldier.cfg'] = true
		cfgfs.intercept_cfgs['pyro.cfg'] = true
		cfgfs.intercept_cfgs['demoman.cfg'] = true
		cfgfs.intercept_cfgs['heavyweapons.cfg'] = true
		cfgfs.intercept_cfgs['engineer.cfg'] = true
		cfgfs.intercept_cfgs['medic.cfg'] = true
		cfgfs.intercept_cfgs['sniper.cfg'] = true
		cfgfs.intercept_cfgs['spy.cfg'] = true
		cfgfs.restore_globals_on_reload['class'] = 'classchange'
		cfgfs.restore_globals_on_reload['slot'] = 'slotchange'
	end

	if os.getenv('GAMENAME') == 'Fistful of Frags' then
		-- https://steamcommunity.com/games/fof/announcements/detail/199616928162078893
		cfgfs.compat_noalias = true
	end
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

-- coroutine -> function(error value)
-- callbacks here are called when the coroutine makes an error
ev_error_handlers = setmetatable({}, {__mode = 'k'}) -- local

-- coroutine -> function(the_coroutine, this_function)
-- callbacks here are called once when the coroutine yields/returns/errors, then removed from the table
-- the callback can re-add itself during the call if it wants to be called next time
-- (this is badly named)
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
		-- this is a table because only one value can be returned from here
		return {err, debug.traceback(err, 2)}
	end
	ev_loop_fn = function (...)
		return xpcall(ev_loop_fn_main, ev_loop_fn_catch, ...)
	end
end
ev_loop_co = coroutine.create(ev_loop_fn)

local ev_handle_return = function (co, ok, rv1, rv2)
	if ok then
		if co == ev_loop_co and rv1 ~= sym_ready then
			ev_loop_co = coroutine.create(ev_loop_fn)
		end
		if rv1 == false and coroutine.status(co) == 'dead' then
			-- note: rv2 is the return value from ev_loop_fn_catch()
			local handler = ev_error_handlers[co]
			if handler then
				ev_error_handlers[co] = nil
				ev_call(handler, rv2[1])
			else
				eprintln('\aerror: %s', rv2[2])
			end
			local ok, err = coroutine.close(co) -- can this yield?
			if not ok then
				eprintln('\aerror: %s', err)
			end
		end
		local cb = ev_return_handlers[co]
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

local num2class = {
	'scout', 'soldier', 'pyro',
	'demoman', 'heavyweapons', 'engineer',
	'medic', 'sniper', 'spy',
}

local class2num = {}

for num, class in ipairs(num2class) do
	class2num[class] = num
end

_lookup_path = function (path)
	local cnt = (unmask_next[path] or 0)
	if cnt > 0 then
		unmask_next[path] = cnt-1
		return 0
	end
	if not (cfgfs.intercept_cfgs[path]
	        or cfgfs.hide_cfgs[path])
	then
		return 0
	end
	return 0xf
end

--------------------------------------------------------------------------------

-- cfg/cmd magic tables

cfg = assert(_cfg)
cfgf = function (fmt, ...) return cfg(string.format(fmt, ...)) end

-- note: these are separate tables so that reassigning a value always calls __newindex()
local cmd_fns = make_resetable_table()
local cmdv_fns = make_resetable_table()
local cmdp_fns = make_resetable_table()

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

local get_cvars = function (t)
	if #t == 0 then
		return {}
	end
	while not game_window_is_active() do
		wait_for_event('attention')
	end
	local rv = {}
	local incnt, outcnt = 0, 0
	for _, k in ipairs(t) do
		if rv[k] == nil then
			cmd.help(k)
			rv[k] = false
			outcnt = outcnt+1
		end
	end
	for _, line in wait_for_events('game_console_output', 1000) do
		local mk, mv = line:match('"([^"]+)" = "([^"]*)"')
		if mk then
			if rv[mk] == false then
				rv[mk] = mv
				incnt = incnt+1
				if mk == 'name' then -- hehe
					own_name_known(mv)
				end
			end
		end
		local mk = line:match('help:  no cvar or command named (.*)$')
		if mk then
			if rv[mk] == false then
				rv[mk] = nil
				incnt = incnt+1
			end
		end
		if incnt == outcnt then
			break
		end
	end
	if incnt ~= outcnt then
		eprintln('warning: could only read %d of %d cvar(s)',
		    incnt, outcnt)
	end
	return rv
end

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
	-- (this is completely untested, i have not used it once)
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

local click_key_bound = false

bind = function (key, cmd, cmd2)
	local n = key2num[key]
	if not n then
		return error(string.format('tried to bind unknown key "%s"', key), 2)
	end
	if cmd == 'cfgfs_click' and key:find('^f[0-9]+$') then
		local cmd = _G.cmd
		click_key_bound = true
		cmd.bind(key, 'exec cfgfs/click')
		_click_set_key(key)
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

local last_event_name = nil

fire_event = function (name, ...)
	local t = events[name]
	if t then
		local copy = {table.unpack(t)}
		for _, cb in ipairs(copy) do
			if t[cb] then
				last_event_name = name
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
				last_event_name = nil
				return ev_resume(this_co)
			end
		end)
	end
	add_listener(name, this_co)
	local t = {coroutine.yield()}
	done = true
	remove_listener(name, this_co)
	return last_event_name, table.unpack(t)
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
					last_event_name = nil
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
					return last_event_name, rv
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

local may_cancel_bind = false
local did_cancel_bind = false
cancel_bind = function ()
	if may_cancel_bind then
		if not did_cancel_bind then
			did_cancel_bind = true
			return true
		else
			return false
		end
	else
		return error('cancel_bind called outside a key event or after yielding inside one', 2)
	end
end

_get_contents = function (path)
	-- keybind?
	local t = bindfilenames[path]
	if t then
		local name = t.name
		if t.type == 'down' then

			is_pressed[name] = _ms()

			local evname = '+'..name
			if events[evname] then
				may_cancel_bind = true
				fire_event(evname, true, name)
				may_cancel_bind = false
				if did_cancel_bind then
					did_cancel_bind = false
					name = ''
				end
			end

			local v = binds_down[name]
			if v then
				if type(v) == 'function' then
					return ev_call(v, true, name)
				else
					return cfg(v)
				end
			end

			return

		elseif t.type == 'up' then

			is_pressed[name] = false

			local evname = '-'..name
			if events[evname] then
				may_cancel_bind = true
				fire_event(evname, false, name)
				may_cancel_bind = false
				if did_cancel_bind then
					did_cancel_bind = false
					name = ''
				end
			end

			local v = binds_up[name]
			if v then
				if type(v) == 'function' then
					return ev_call(v, false, name)
				else
					return cfg(v)
				end
			end

			return

		elseif t.type == 'toggle' then
			if is_pressed[name] then
				is_pressed[name] = false

				local evname = '-'..name
				if events[evname] then
					may_cancel_bind = true
					fire_event(evname, false, name)
					may_cancel_bind = false
					if did_cancel_bind then
						did_cancel_bind = false
						name = ''
					end
				end

				local v = binds_up[name]
				if v then
					if type(v) == 'function' then
						return ev_call(v, false, name)
					else
						return cfg(v)
					end
				end
			else
				is_pressed[name] = _ms()

				local evname = '+'..name
				if events[evname] then
					may_cancel_bind = true
					fire_event(evname, true, name)
					may_cancel_bind = false
					if did_cancel_bind then
						did_cancel_bind = false
						name = ''
					end
				end

				local v = binds_down[name]
				if v then
					if type(v) == 'function' then
						return ev_call(v, true, name)
					else
						return cfg(v)
					end
				end
			end
			return
		elseif t.type == 'once' then
			do
				is_pressed[name] = _ms()

				local evname = '+'..name
				local name = name
				if events[evname] then
					may_cancel_bind = true
					fire_event(evname, true, name)
					may_cancel_bind = false
					if did_cancel_bind then
						did_cancel_bind = false
						name = ''
					end
				end

				local v = binds_down[name]
				if v then
					if type(v) == 'function' then
						ev_call(v, true, name)
					else
						cfg(v)
					end
				end
			end
			do
				is_pressed[name] = false

				local evname = '-'..name
				local name = name
				if events[evname] then
					may_cancel_bind = true
					fire_event(evname, false, name)
					may_cancel_bind = false
					if did_cancel_bind then
						did_cancel_bind = false
						name = ''
					end
				end

				local v = binds_up[name]
				if v then
					if type(v) == 'function' then
						return ev_call(v, false, name)
					else
						return cfg(v)
					end
				end
			end
			return
		else
			return fatal('unknown bind type')
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
			local f = assert(io.open('LICENSE', 'r'))
			for line in f:lines() do
				cmd.echo(line)
			end
			f:close()
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

		if not cfgfs.hide_cfgs[path] then
			cfgf('exec"cfgfs/unmask_next/%s";exec"%s"', path, path)
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

local is_active = false

_attention = function (new_is_active)
	fire_event('attention', new_is_active)
	is_active = new_is_active
end

is_game_window_active = function ()
	return is_active
end
game_window_is_active = function ()
	return is_active
end

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

local attention_message_shown = false
local yield_id = 1

_cli_input = function (line)

	if lua_mode and line == ']]' then
		eprintln('Entered cfg mode. Type ">>" to return to Lua mode.')
		lua_mode = false
		_set_prompt('] ')
		return
	elseif not lua_mode and line == '>>' then
		eprintln('Entered Lua mode. Type "]]" to return to cfg mode.')
		lua_mode = true
		_set_prompt('> ')
		return
	end

	if line == 'cfgfs_license' then
		local f = assert(io.open('LICENSE', 'r'))
		for line in f:lines() do
			printv(line)
		end
		f:close()
		return
	end

	local buffer_was_empty = _buffer_is_empty()

	if lua_mode then
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

--------------------------------------------------------------------------------

local our_logfile
if os.getenv('CFGFS_STARTTIME') then
	os.execute('mkdir -p logs')
	logfilename = os.date('logs/console_%Y-%m-%d_%H:%M:%S.log', tonumber(os.getenv('CFGFS_STARTTIME')))
	our_logfile = io.open(logfilename, 'a')
end
if not our_logfile then
	logfilename = 'console.log'
	our_logfile = assert(io.open(logfilename, 'a'))
end
our_logfile:setvbuf('line')

--------------------------------------------------------------------------------

-- terminal printing / coloring / etc

local name2team = {}
local myname = nil

local team2color = {
	['red'] = '34;38;2;184;56;59',
	['blu'] = '34;38;2;88;133;162',
	['unknown'] = '1',
}
local team2opposite = {
	['red'] = 'blu',
	['blu'] = 'red',
}

team_known = function (name, team)
	if team2opposite[team] then
		name2team[name] = team
	else
		error('team_known: invalid team ' .. team)
	end
end
own_name_known = function (name)
	myname = name
end

local register_kill = function (n1, n2)
	if (not not name2team[n1]) == (not not name2team[n2]) then
		-- both either known or not known -> nothing to do
		if name2team[n1] and (name2team[n1] == name2team[n2]) then
			-- both are on the same team, probably got confused somehow
			name2team[n1] = nil
			name2team[n2] = nil
			return
		end
		return
	end
	if name2team[n1] then
		name2team[n2] = team2opposite[name2team[n1]]
	else
		name2team[n1] = team2opposite[name2team[n2]]
	end
end
local register_autobalance = function (name)
	if name2team[name] then
		name2team[name] = team2opposite[name2team[name]]
	end
end
local register_self_autobalance = function ()
	register_autobalance(myname)
end
local clear_name = function (name)
	name2team[name] = nil
end
local colorize_team = function (name)
	return string.format('\27[%sm%s\27[0m',
	    team2color[name2team[name] or 'unknown'],
	    name)
end

_game_console_output = function (line)
	-- detect some useless error messages
	local spam = false
	if #line >= 19 then
		local c = line:sub(1, 1)
		if c == '\n' and #line >= 57 and line:find('^\n ##### CTexture::LoadTextureBitsFromFile couldn\'t find ') then goto match end
		if c == '*' and #line >= 41 and line:find('^%*%*%* Invalid sample rate %([0-9]+%) for sound \'.*\'%.$') then goto match end
		if c == '-' and #line >= 27 and line:find('^%-%-%- Missing Vgui material ') then goto match end
		if c == 'A' and #line >= 48 and line:find('^Attemped to precache unknown particle system ".*"!$') then goto match end
		if c == 'C' and #line >= 23 and line:find('^Could not find table ".*"$') then goto match end
		if c == 'C' and #line >= 44 and line:find('^Cannot update control point [0-9]+ for effect \'.*\'%.$') then goto match end
		if c == 'C' and #line >= 99 and line:find('^Convar .* has conflicting FCVAR_CHEAT flags %(child: FCVAR_CHEAT, parent: no FCVAR_CHEAT, parent wins%)$') then goto match end
		if c == 'E' and #line >= 34 and line:find('^EmitSound: pitch out of bounds = %-?[0-9]+$') then goto match end
		if c == 'E' and #line >= 41 and line:find('^Error: Material ".*" uses unknown shader ".*"$') then goto match end
		if c == 'E' and #line >= 54 and line:find('^Error! Variable ".*" is multiply defined in material ".*"!$') then goto match end
		if c == 'F' and #line >= 37 and line:find('^Failed to create decoder for MP3 %[ .* %]$') then goto match end
		if c == 'F' and #line >= 72 and line:find('^For FCVAR_REPLICATED, ConVar must be defined in client and game .dlls %(.*%)$') then goto match end
		if c == 'F' and #line >= 102 and line:find('^Failed to find attachment point specified for .* event%. Trying to spawn effect \'.*\' on attachment named \'.*\'$') then goto match end
		if c == 'M' and #line >= 65 and line:find('^MP3 initialized with no sound cache, this may cause janking%. %[ .* %]$') then goto match end
		if c == 'M' and #line >= 68 and line:find('^Model \'.*\' doesn\'t have attachment \'.*\' to attach particle system \'.*\' to%.$') then goto match end
		if c == 'N' and #line >= 35 and line:find('^No such variable ".*" for material ".*"$') then goto match end
		if c == 'P' and #line >= 40 and line:find('^Parent cvar in server.dll not allowed %(.*%)$') then goto match end
		if c == 'R' and #line >= 78 and line:find('^Requesting texture value from var ".*" which is not a texture value %(material: .*%)$') then goto match end
		if c == 'S' and #line >= 36 and line:find('^Shutdown function [A-Za-z0-9_]+%(%) not in list!!!$') then goto match end
		if c == 'S' and #line >= 49 and line:find('^SetupBones: invalid bone array size %([0-9]+ %- needs [0-9]+%)$') then goto match end
		if c == 'U' and #line >= 19 and line:find('^Unable to remove .+!$') then goto match end
		if c == 'U' and #line >= 55 and line:find('^Unable to bind a key for command ".*" after [0-9]+ attempt%(s%)%.$') then goto match end
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
		-- copy the line so we can modify it (and write the original to the log)
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
		else
			-- more colorizing trash

			local bright = function (s) return string.format('\27[1m%s\27[0m', s) end
			do

			-- kill (non-crit)
			local killer, target, weapon = line:match('^(.+) killed (.+) with (.+)%.$')
			if killer then
				register_kill(killer, target)
				line = string.format('%s killed %s with %s.',
				    colorize_team(killer),
				    colorize_team(target),
				    bright(weapon))
				goto matched
			end
			-- kill (crit)
			local killer, target, weapon = line:match('^(.+) killed (.+) with (.+)%. %(crit%)$')
			if killer then
				register_kill(killer, target)
				line = string.format('%s killed %s with %s. \27[1;5;37;41m(crit)\27[0m',
				    colorize_team(killer),
				    colorize_team(target),
				    bright(weapon))
				goto matched
			end
			-- s*icide
			local target = line:match('^(.+) suicided%.$')
			if target then
				line = string.format('%s suicided.',
				    colorize_team(target))
				goto matched
			end

			-- chat message
			-- i think i know why the separator is " :  " with two
			--  spaces now
			-- normally you can't put two consecutive spaces in your
			--  name so this way it's never ambiguous where the name
			--  ends
			local name, rest = line:match('^(.+)( :  .+)$')
			if name then
				local pre =
				    line:match('^%(TEAM%) ') or
				    line:match('^%*DEAD%*%(TEAM%) ') or
				    line:match('^%*DEAD%* ') or
				    line:match('^%*SPEC%* ')
				if pre and #name-#pre >= 1 then
					name = name:sub(#pre+1)
				else
					pre = ''
				end
				line = pre .. colorize_team(name) .. rest
				goto matched
			end

			-- connected
			local name = line:match('^(.+) connected$')
			if name then
				clear_name(name)
				line = string.format('%s connected',
				    colorize_team(name))
				goto matched
			end
			-- autobalanced
			local name = line:match('^(.+) was moved to the other team for game balance$')
			if name then
				register_autobalance(name)
				line = string.format('%s was moved to the other team for game balance',
				    colorize_team(name))
				goto matched
			end

			-- self autobalanced
			if #line >= 110 and line:find('^You have switched to team [BR][EL][DU] and will receive [0-9]+ experience points at the end of the round for changing teams%.$') then
				register_self_autobalance()
				goto matched
			end

			-- joining a server
			if line:find('^\n.*\nMap: .*\nPlayers: .* / .*\nBuild: .*\nServer Number: .*\n$') then
				name2team = {}
				goto matched
			end

			end
			::matched::
		end

		-- clean up newline spam
		if line:find('\n', 1, true) then
			line = line
			    :gsub('\n[\n ]*', '\n')
			    :gsub('^\n+', '', 1)
			    :gsub('\n+$', '', 1)
		end

		-- make crit kills blink
		--line = line:gsub(' %(crit%)$', ' \27[1;5;37;41m(crit)\27[0m', 1)

		_println(line)
	else
		-- 2 = dim
		printv('\27[2m', line, '\27[0m')
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
	return fire_event('control', line)
end

--------------------------------------------------------------------------------

local logpos = 0
do
	local f = assert(io.open(logfilename, 'r'))
	logpos = assert(f:seek('end'))
	f:close()
end

open_log = function ()
	local f = assert(io.open(logfilename, 'r'))
	assert(f:seek('set', logpos))
	return f
end

grep = function (pat)
	local f = open_log()
	for line in f:lines() do
		if line:find(pat) then
			printv(line)
		end
	end
	f:close()
end

--------------------------------------------------------------------------------

-- local click_key_bound: declared near bind() where it's set

local before_script_exec = function ()
	click_key_bound = false
end
local after_script_exec = function ()
	cmd.cfgfs_click = 'exec cfgfs/click'
	cmd.cfgfs_init = 'exec cfgfs/init'

	-- this is a risky operation. it works most of the time but has a chance of freezing the game
	-- resetting con_logfile closes the file inside the game, and it's re-opened on the next console write
	-- sometimes something goes wrong and the game deadlocks instead
	-- note: the log is normally inited from launch options, this alias exists to do it on cfgfs startup if it had crashed
	cmd.cfgfs_init_log = function ()
		cvar.con_logfile = ''
		cvar.con_logfile = 'console.log'
		cmd.echo('cfgfs: log file has been reinited')
	end

	cmd.cfgfs_license = 'exec cfgfs/license'
	cmd.cfgfs_restart = function ()
		cvar.con_logfile = ''
		wait(0)
		os.exit(1)
	end
	cmd.cfgfs_source = function () return cmd.echo(agpl_source_url) end
	cmd.release_all_keys = assert(release_all_keys)
	if not click_key_bound then
		eprintln('\awarning: no function key bound to "cfgfs_click" found!')
		eprintln(' why: one of the f1-f12 keys must be bound to "cfgfs_click" for delayed command execution to work')
		eprintln(' how: add a keybind like "bind(\'f11\', \'cfgfs_click\')" and re-save script.lua')
	end
end

-- reload and re-run script.lua
_reload_1 = function ()
	local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or 'script.lua')
	if not ok then
		eprintln('\aerror: %s', err)
		eprintln('failed to reload script.lua!')
		return false
	end
	local script = ok

	_fire_unload(false)
	do_reset()

	before_script_exec()
	local ok, err = xpcall(script, debug.traceback)
	after_script_exec()

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

	_init()

	for name, evt in pairs(cfgfs.restore_globals_on_reload) do
		if global(name) then
			fire_event(evt, global(name))
		end
	end

	return fire_event('reload')
end

_fire_startup = function ()
	-- probably crashed. need to init manually and reopen the log file
	if os.getenv('CFGFS_RESTARTED') then
		cfg('cfgfs_init_log; cfgfs_init')
	end
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

local ok, err = loadfile(os.getenv('CFGFS_SCRIPT') or 'script.lua')
if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end
local script = ok

before_script_exec()
local ok, err = xpcall(script, debug.traceback)
after_script_exec()

if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end

--------------------------------------------------------------------------------

collectgarbage()

return true
