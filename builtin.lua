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
			local co, is_main = coroutine.running()
			assert(not is_main, 'tried to get panic function outside a coroutine')
			return ev_error_handlers[co]
		end
		return error('tried to access nonexistent variable ' .. tostring(k), 2)
	end,
	__newindex = function (self, k, v)
		if k == 'panic' then
			local co, is_main = coroutine.running()
			assert(not is_main, 'tried to set panic function outside a coroutine')
			assert(v == nil or type(v) == 'function', 'panic function must be a function')
			ev_error_handlers[co] = v
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
		notify_cfgs = {
			['config.cfg'] = true,
		},

		block_cfgs = {
			-- ['name.cfg'] = true,
		},

		restore_globals_on_reload = {
			-- ['varname'] = 'eventname',
		},

		compat_noalias = false,
	}

	if os.getenv('GAMENAME') == 'Team Fortress 2' or
	   os.getenv('GAMENAME') == 'Team Fortress 2 Classic' then
		cfgfs.notify_cfgs['scout.cfg'] = true
		cfgfs.notify_cfgs['soldier.cfg'] = true
		cfgfs.notify_cfgs['pyro.cfg'] = true
		cfgfs.notify_cfgs['demoman.cfg'] = true
		cfgfs.notify_cfgs['heavyweapons.cfg'] = true
		cfgfs.notify_cfgs['engineer.cfg'] = true
		cfgfs.notify_cfgs['medic.cfg'] = true
		cfgfs.notify_cfgs['sniper.cfg'] = true
		cfgfs.notify_cfgs['spy.cfg'] = true
		if os.getenv('GAMENAME') == 'Team Fortress 2 Classic' then
			cfgfs.notify_cfgs['civilian.cfg'] = true
		end
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

-- list of coroutines waiting to be resumed
-- contains objects with properties:
--   co: the coroutine
--   target: timestamp of when it should be resumed
--   gen: value of ev_generation_counter when it was added
local ev_timeouts = make_resetable_table()

-- counter for ev_do_timeouts() to know if a timeout was added during the same event loop iteration (-> should not resume it)
-- timeouts added to the ev_timeouts table have a "gen" property set to whatever this was when the timeout was added
local ev_generation_counter = 1
add_reset_callback(function ()
	-- note: all timeouts (= references to past values) are removed on reset so this is safe to reset too
	ev_generation_counter = 1
end)

-- coroutine -> function(error value)
-- callbacks here are called when the coroutine makes an error
ev_error_handlers = setmetatable({}, {__mode = 'k'}) -- local

-- coroutine -> function(the_coroutine, this_function)
-- callbacks here are called once when the coroutine yields/returns/errors, then removed from the table
-- the callback can re-add itself during the call if it wants to be called next time
-- (this is badly named)
local ev_return_handlers = setmetatable({}, {__mode = 'k'})

-- coroutine -> function it's running
-- (for ev_find_origin)
local ev_co_to_fn = setmetatable({}, {__mode = 'kv'})

-- unique values for yielding
local sym_ready = {} -- ev_loop_co is ready to be reused
local sym_wait = {} -- coroutine used wait() and put itself in the list

-- ev_loop_co: used for calling functions in a coroutine context without
--  creating a new coroutine each time for that
-- if the function yields, ev_loop_co gets replaced with a new coroutine
local ev_loop_co
local ev_loop_fn

local ev_call

do
	local ev_loop_fn_main = function (...)
		local args = {...}
		local this_co = coroutine.running()
		while true do
			local fn = args[1]
			ev_co_to_fn[this_co] = fn
			fn(select(2, table.unpack(args)))

			-- the thing we called used yield so we're not the main
			--  coroutine anymore
			if ev_loop_co ~= this_co then
				break
			end

			-- clear state, pretend this is a new coroutine
			ev_error_handlers[this_co] = nil
			ev_return_handlers[this_co] = nil

			args = {coroutine.yield(sym_ready)}
		end
	end
	local ev_loop_fn_catch = function (err)
		-- note: the two return values are in a table because this
		--  function can only return one value
		return {err, debug.traceback(err, 2)}
	end
	ev_loop_fn = function (...)
		return xpcall(ev_loop_fn_main, ev_loop_fn_catch, ...)
	end
end
ev_loop_co = coroutine.create(ev_loop_fn)

local ev_handle_return = function (co, ok, rv1, rv2)
	if ok then
		-- user coroutine yielded/erred?
		if co == ev_loop_co and rv1 ~= sym_ready then
			ev_loop_co = coroutine.create(ev_loop_fn)
		end

		-- pcall threw up and the coroutine died?
		if rv1 == false and coroutine.status(co) == 'dead' then
			-- note: rv2 is the return value from ev_loop_fn_catch()

			-- someone wants to know about this error?
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

		-- someone wants to know about this return?
		local cb = ev_return_handlers[co]
		if cb then
			ev_return_handlers[co] = nil
			return ev_call(cb, co, cb)
		end
	else
		-- some kind of a programmer error. ev_loop_fn uses pcall so we
		--  should never get here

		return error(rv1)
	end
end

ev_call = function (fn, ...) -- note: this is declared as local above
	if coroutine.running() == ev_loop_co then
		ev_loop_co = coroutine.create(ev_loop_fn)
	end
	return ev_handle_return(ev_loop_co, coroutine.resume(ev_loop_co, fn, ...))
end
local ev_resume = function (co, ...)
	return ev_handle_return(co, coroutine.resume(co, ...))
end

-- find the "file and line defined" for a coroutine/function (for debugging)
local ev_find_origin = function (v)
	if type(v) == 'thread' then
		v = ev_co_to_fn[v]
	end
	if type(v) == 'function' then
		local t = debug.getinfo(v)
		if not t.short_src:find('^%[') then
			return string.format('%s:%s',
			    t.short_src:match('[^/]+$'),
			    t.linedefined)
		else
			return t.short_src
		end
	end
	return 'unknown'
end

-- canceldata: optionally set to an empty table if the wait should be cancellable.
-- cancellation is done by calling cancel_wait() with that table from a different coroutine.
-- if the wait is cancelled, the waiting coroutine will be woken up early and get a return value of false from wait()

wait = function (ms, canceldata)
	local target = _ms()+ms
	local click_id = (_click(ms) or true)
	local check_cancel = false
	local this_co, is_main = coroutine.running()
	assert(not is_main, 'tried to wait() outside a coroutine')

	-- click_id is the "id" of the click. it's either userdata for
	--  pthread_t, or true if the click didn't spawn a thread ("ms" was 0
	--  and do_click() was called directly)

	if type(canceldata) == 'table' then
		assert(nil == next(canceldata), 'wait: canceldata is not empty')
		canceldata.co = this_co
		canceldata.id = click_id
		check_cancel = true
	end

	table.insert(ev_timeouts, {
		gen    = ev_generation_counter,
		target = target,
		co     = this_co,

		-- for informative purposes
		delay = ms,
	})

	coroutine.yield(sym_wait)

	-- was it a cancellable wait?
	if check_cancel then
		local stored_id = canceldata.id
		-- same type (userdata/pthread_t or true)
		if type(stored_id) == type(click_id) then
			if stored_id == click_id then
				-- ok: wait was NOT cancelled
				canceldata.co = nil
				canceldata.id = nil
				return true
			else
				-- error: reused the table for a different wait. don't do this
				return error('wait: illegal reuse of canceldata', 2)
			end
		elseif stored_id == nil then
			-- ok: the wait was cancelled
			assert(canceldata.co == nil, 'wait: cancelled without clearing canceldata.co')
			return false
		else
			return error('wait: canceldata.id has wrong type ' .. type(stored_id), 2)
		end
	end

	return true
end

_list_timeouts = function ()
	local now = _ms()
	for i, t in ipairs(ev_timeouts) do
		local status
		if t.target > now then
			status = string.format('+%dms',
			    math.floor(t.target-now))
		else
			status = string.format('-%dms!',
			    math.floor(now-t.target))
		end
		println('\27[1m%d\27[0m: <%s> (%s) gen=%d wait=%d (%s)',
		    i,
		    t.co, ev_find_origin(t.co),
		    t.gen, math.floor(t.delay),
		    status)
	end
	println('total %d', #ev_timeouts)
end

local ev_delete_timeout_for_co = function (co)
	local ts = ev_timeouts
	for i = 1, #ts do
		if ts[i].co == co then
			table.remove(ts, i)
			return true
		end
	end
	return false
end

cancel_wait = function (canceldata)
	if type(canceldata) == 'table' then
		local id = canceldata.id
		if id == true or type(id) == 'userdata' then
			-- ok: in these cases, the coroutine is still waiting to be resumed
			-- if id is userdata, then there's a thread we might be able to cancel to save a useless click()

			local co = canceldata.co

			if type(id) == 'userdata' then
				-- cancel the thread to potentially avoid a useless click
				_click_cancel(id)
			end

			-- remove the coroutine from the timeouts list
			assert(ev_delete_timeout_for_co(co))

			-- clear canceldata and resume the coroutine
			canceldata.co = nil
			canceldata.id = nil
			ev_resume(co)

			return
		elseif id == nil then
			-- ok: click had already been cancelled or it had expired
			return
		else
			return error('cancel_wait: canceldata.id has wrong type ' .. type(id), 2)
		end
	end
	return error('cancel_wait: canceldata has wrong type ' .. type(canceldata), 2)
end

local ev_do_timeouts = function ()
	if #ev_timeouts == 0 then return end

	local cur_gen = ev_generation_counter
	ev_generation_counter = cur_gen+1

	-- do all timeouts that are both
	-- * expired (now >= t.target)
	-- * added before this call to ev_do_timeouts() (t.gen <= cur_gen)
	-- note: the loop is restarted after each timeout because they can also
	--  modify the list (adding and removing entries from any position)
	-- the generation counter exists so this can be done without ending up
	--  in an infinite loop calling new timeouts that are already expired
::again::
	local now = _ms()
	for i, t in ipairs(ev_timeouts) do
		if now >= t.target and t.gen <= cur_gen then
			table.remove(ev_timeouts, i)

			-- warn if it gets badly delayed for whatever reason
			local delay = now-t.target
			local three_frames = 1000/120*3
			if delay > three_frames then
				eprintln('warning: wait(%d) at %s was delayed by %.2f ms!',
				    math.floor(t.delay), ev_find_origin(t.co), delay)
			end

			ev_resume(t.co)
			goto again
		end
	end
end

spinoff = ev_call

--------------------------------------------------------------------------------

-- event stuff

local events = make_resetable_table()

add_listener = function (name, cb)
	if type(name) == 'string' then
		local t = events[name]
		if t then
			if not t[cb] then
				table.insert(t, cb)
				t[cb] = true
			end
		else
			events[name] = {[1] = cb, [cb] = true}
		end
	elseif type(name) == 'table' then
		for _, name in ipairs(name) do
			add_listener(name, cb)
		end
	else
		return error('add_listener: name must be a string or list of strings', 2)
	end
end

_list_events = function ()
	local evnames = {}
	for k in pairs(events) do
		table.insert(evnames, k)
	end
	table.sort(evnames)

	local cbtotal = 0
	for _, name in ipairs(evnames) do
		println('\27[1m%s\27[0m: %d listener(s)', name, #events[name])
		for i, v in ipairs(events[name]) do
			println('  \27[1m%d\27[0m: <%s> (%s)',
			    i, v, ev_find_origin(v))
		end
		cbtotal = cbtotal+#events[name]
	end
	println('total %d events %d listeners',
	    #evnames, cbtotal)
end

remove_listener = function (name, cb)
	if type(name) == 'string' then
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
	elseif type(name) == 'table' then
		for _, name in ipairs(name) do
			remove_listener(name, cb)
		end
	else
		return error('remove_listener: name must be a string or list of strings', 2)
	end
end

local last_event_name = nil

local in_event = false
local canceldata = nil

fire_event = function (name, ...)
	local rv = 0
	local t = events[name]
	if t then
		local copy = {table.unpack(t)}
		for _, cb in ipairs(copy) do
			if t[cb] then
				last_event_name = name

				local old_in_event = in_event
				local old_canceldata = canceldata
				in_event = true
				canceldata = nil

				if type(cb) == 'function' then
					ev_call(cb, ...)
				elseif type(cb) == 'thread' then
					ev_resume(cb, ...)
				end
				rv = rv+1

				local my_canceldata = canceldata
				in_event = old_in_event
				canceldata = old_canceldata

				if my_canceldata then
					return -rv, my_canceldata
				end
			end
		end
	end
	return rv
end

cancel_event = function (data)
	if in_event then
		canceldata = data or true
	else
		return error('tried to cancel_event() outside an event handler', 2)
	end
end

wait_for_event = function (name, timeout_opt)
	local this_co, is_main = coroutine.running()
	if not is_main then
		local canceldata = {}
		if timeout_opt then
			spinoff(function ()
				if wait(timeout_opt, canceldata) then
					last_event_name = nil
					return ev_resume(this_co)
				end
			end)
		end
		add_listener(name, this_co)
		local t = {coroutine.yield()}
		remove_listener(name, this_co)
		cancel_wait(canceldata)
		return last_event_name, table.unpack(t)
	else
		return error('tried to wait_for_event() outside a coroutine', 2)
	end
end

-- like wait_for_event() but usable in a for-in loop
wait_for_events = function (name, timeout_opt)
	return function ()
		return wait_for_event(name, timeout_opt)
	end
end

--------------------------------------------------------------------------------

-- cfg/cmd magic tables

cfg = assert(_cfg)
cfgf = function (fmt, ...) return cfg(string.format(fmt, ...)) end

-- note: these are separate tables so that assigning an existing value calls
--  __newindex() too
local cmd_fns = make_resetable_table()
local cmdv_fns = make_resetable_table()
local cmdp_fns = make_resetable_table()

-- used by cfgfs_readdir()
_defined_alias_names = make_resetable_table()

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
				_defined_alias_names[k] = true
			elseif v ~= nil then
				v = tostring(v)
				cmd_fns[k] = function () return cfg(v) end
				cmd.alias(k, 'exec', 'cfgfs/alias/'..k)
				_defined_alias_names[k] = true
			else
				cmd_fns[k] = nil
				cmd.alias(k, '')
				-- ^ there's no "unalias" command so just set it to empty
				_defined_alias_names[k] = nil
			end
		else
			if type(v) == 'function' then
				cmd_fns[k] = v
				_defined_alias_names[k] = true
			elseif v ~= nil then
				v = tostring(v)
				cmd_fns[k] = function () return cfg(v) end
				_defined_alias_names[k] = true
			else
				cmd_fns[k] = nil
				_defined_alias_names[k] = nil
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
	if __linux__ then
		while not game_window_is_active() do
			wait_for_event('attention')
		end
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
	for ev, line in wait_for_events({'game_console_output', 'game_console_output_jumbled'}, 1000) do
		if ev == 'game_console_output_jumbled' then
			local mk, mv = line:match('^"([^"]+)" = "(.*)"$')
			if mk then
				if rv[mk] == false then
					rv[mk] = mv
					incnt = incnt+1
					fire_event('cvar.'..mk, mv)
				end
			end
		elseif ev == 'game_console_output' then
			local mk = line:match('^help:  no cvar or command named (.*)$')
			if mk then
				if rv[mk] == false then
					rv[mk] = nil
					incnt = incnt+1
				end
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

local binds_down = make_resetable_table()
local binds_up = make_resetable_table()

local click_key_bound = false

bind = function (key, cmd, cmd2)
	local n = key2num[key]
	if not n then
		return error(string.format('tried to bind unknown key "%s"', key), 2)
	end

	-- setting the click key?
	if cmd == 'cfgfs_click' and key:find('^f[0-9]+$') then
		local cmd = _G.cmd
		click_key_bound = true
		cmd.bind(key, 'exec cfgfs/click')
		_click_set_key(key)
		return
	end

	-- set the bind and create the alias
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

is_pressed = setmetatable({}, {
	__index = function (_, key)
		return error(string.format('unknown key "%s"', key), 2)
	end,
})
for key in pairs(key2num) do
	is_pressed[key] = false
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
				if fire_event(evname, true, name) < 0 then
					return
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
				if fire_event(evname, false, name) < 0 then
					return
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
					if fire_event(evname, false, name) < 0 then
						return
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
					if fire_event(evname, true, name) < 0 then
						return
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
					if fire_event(evname, true, name) < 0 then
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
					if fire_event(evname, false, name) < 0 then
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
		_click_received()
		return ev_do_timeouts()
	end
	if path == '/cfgfs/buffer.cfg' then
		-- nothing to do, buffer contents are returned automatically
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

		if path == 'license.cfg' then
			local f = assert(io.open((os.getenv('CFGFS_DIR') or '.')..'/LICENSE', 'r'))
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
		-- is it config.cfg? output our license message for that
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

		-- is this a class config?
		local cls = (path:before('.cfg') or path)
		if ({
			['scout'] = true,
			['soldier'] = true,
			['pyro'] = true,
			['demoman'] = true,
			['heavyweapons'] = true,
			['engineer'] = true,
			['medic'] = true,
			['sniper'] = true,
			['spy'] = true,
		})[cls] then
			fire_event('classchange', cls)
		end

		-- should exec the real one?
		if not cfgfs.block_cfgs[path] then
			cfgf('exec"cfgfs/unmask_next/%s";exec"%s"', path, path)
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
	is_active = new_is_active
	fire_event('attention', new_is_active)
	collectgarbage()
end

is_game_window_active = function ()
	return is_active
end
game_window_is_active = function ()
	return is_active
end

end

--------------------------------------------------------------------------------

-- cli input handling

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
		local f = assert(io.open((os.getenv('CFGFS_DIR') or '.')..'/LICENSE', 'r'))
		for line in f:lines() do
			printv(line)
		end
		f:close()
		return
	end

	local fn_to_run = nil
	if lua_mode then
		local fn, err = repl_fn(line)
		if not fn then
			eprintln('%s', err)
			return
		end
		fn_to_run = fn
	end

	local buffer_was_empty = _buffer_is_empty()

	if fn_to_run then
		local id = yield_id
		local cb_initial
		local cb_yielded
		cb_initial = function (co, cb)
			if co ~= ev_loop_co and coroutine.status(co) == 'suspended' then
				eprintln('\27[1;34m->\27[0m %d', id)
				yield_id = yield_id+1
				ev_return_handlers[co] = cb_yielded

				-- ... maybe this should check ev_timeouts or
				--  something
				-- at least things waiting for sh() processes do
				--  get resumed early
				if  __linux__
				and not is_game_window_active()
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
		ev_call(fn_to_run)
		-- note: the handler callbacks are automatically removed if the
		--  function returns without yielding
	else
		cfg(line)
	end

	if  __linux__
	and buffer_was_empty
	and not _buffer_is_empty()
	and not is_game_window_active()
	and not attention_message_shown
	then
		println('note: commands won\'t be executed until the game window is activated')
		attention_message_shown = true
	end

	collectgarbage()

end

--------------------------------------------------------------------------------

local our_logfile
if os.getenv('CFGFS_STARTTIME') then
	if __linux__ then
		os.execute('mkdir -p logs')
	else
		-- windows: cygwin /bin might not be in $PATH, call it directly
		os.execute('/bin/mkdir -p logs')
	end
	logfilename = os.date('logs/console_%Y-%m-%d_%H:%M:%S.log', tonumber(os.getenv('CFGFS_STARTTIME')))
	our_logfile = io.open(logfilename, 'a')
end
if not our_logfile then
	logfilename = 'console.log'
	our_logfile = assert(io.open(logfilename, 'a'))
end
our_logfile:setvbuf('line')

--------------------------------------------------------------------------------

-- split chunked input into lines

local linereader = function (got_line)
	local data = ''
	return function (s)
		if s then
			local i = 1
			while i <= #s do
				local nlpos = s:find('\n', i, true)
				if nlpos then
					if data == '' then
						got_line(s:sub(i, nlpos-1))
					else
						local s = data .. s:sub(i, nlpos-1)
						data = ''
						got_line(s)
					end
					i = nlpos+1
				else
					data = data .. s:sub(i)
					break
				end
			end
		else
			if data ~= '' then
				-- careful: empty data before the call, in case
				--  it calls us again
				local s = data
				data = ''
				got_line(s)
			end
			got_line(nil)
		end
	end
end

local allreader = function (got_line)
	local data = {}
	return function (s)
		if s then
			table.insert(data, s)
		else
			got_line(table.concat(data, ''))
			data = {}
		end
	end
end

--------------------------------------------------------------------------------

-- terminal printing / coloring / etc

local is_spam = function (line)
	-- spammy errors there's nothing for us to do about
	if #line >= 18 then
		local c = line:sub(1, 1)
		if c == '\n' and #line >= 56 and line:find('^\n ##### CTexture::LoadTextureBitsFromFile couldn\'t find .*$') then goto match end
		if c == '*' and #line >= 41 and line:find('^%*%*%* Invalid sample rate %(%-?[0-9]+%) for sound \'.*\'%.$') then goto match end
		if c == '-' and #line >= 26 and line:find('^%-%-%- Missing Vgui material .*$') then goto match end
		if c == 'A' and #line >= 48 and line:find('^Attemped to precache unknown particle system ".*"!$') then goto match end
		if c == 'A' and #line >= 65 and line:find('^Attempt to set particle collection .* to invalid orientation matrix$') then goto match end
		if c == 'C' and #line >= 23 and line:find('^Could not find table ".*"$') then goto match end
		if c == 'C' and #line >= 44 and line:find('^Cannot update control point %-?[0-9]+ for effect \'.*\'%.$') then goto match end
		if c == 'C' and #line >= 74 and line:find('^Convar .* has conflicting FCVAR_CHEAT flags %(child: .*, parent: .*, parent wins%)$') then goto match end
		if c == 'E' and #line >= 34 and line:find('^EmitSound: pitch out of bounds = %-?[0-9]+$') then goto match end
		if c == 'E' and #line >= 41 and line:find('^Error: Material ".*" uses unknown shader ".*"$') then goto match end
		if c == 'E' and #line >= 51 and line:find('^Error: Material ".*" : proxy ".*" unable to initialize!$') then goto match end
		if c == 'E' and #line >= 54 and line:find('^Error! Variable ".*" is multiply defined in material ".*"!$') then goto match end
		if c == 'F' and #line >= 37 and line:find('^Failed to create decoder for MP3 %[ .* %]$') then goto match end
		if c == 'F' and #line >= 67 and line:find('^Failed to load sound ".*", file probably missing from disk/repository$') then goto match end
		if c == 'F' and #line >= 72 and line:find('^For FCVAR_REPLICATED, ConVar must be defined in client and game %.dlls %(.*%)$') then goto match end
		if c == 'F' and #line >= 130 and line:find('^Failed to find attachment point specified for AE_CL_CREATE_PARTICLE_EFFECT event%. Trying to spawn effect \'.*\' on attachment named \'.*\'$') then goto match end
		if c == 'I' and #line >= 69 and line:find('^Ignoring unreasonable position %(%-?[0-9]+%.[0-9]+,%-?[0-9]+%.[0-9]+,%-?[0-9]+%.[0-9]+%) from vphysics! %(entity .*%)$') then goto match end
		if c == 'M' and #line >= 25 and line:find('^Missing RecvProp for .* %- .*/.*$') then goto match end
		if c == 'M' and #line >= 39 and line:find('^MDLCache: Failed load of %.PHY data for .*$') then goto match end
		if c == 'M' and #line >= 65 and line:find('^MP3 initialized with no sound cache, this may cause janking%. %[ .* %]$') then goto match end
		if c == 'M' and #line >= 68 and line:find('^Model \'.*\' doesn\'t have attachment \'.*\' to attach particle system \'.*\' to%.$') then goto match end
		if c == 'N' and #line >= 35 and line:find('^No such variable ".*" for material ".*"$') then goto match end
		if c == 'P' and #line >= 40 and line:find('^Parent cvar in server%.dll not allowed %(.*%)$') then goto match end
		if c == 'R' and #line >= 78 and line:find('^Requesting texture value from var ".*" which is not a texture value %(material: .*%)$') then goto match end
		if c == 'S' and #line >= 33 and line:find('^Shutdown function .* not in list!!!$') then goto match end
		if c == 'S' and #line >= 49 and line:find('^SetupBones: invalid bone array size %(%-?[0-9]+ %- needs %-?[0-9]+%)$') then goto match end
		if c == 'S' and #line >= 51 and line:find('^Shader \'.*\' %- Couldn\'t load combo %-?[0-9]+ of shader %(dyn=%-?[0-9]+%)$') then goto match end
		if c == 'S' and #line >= 53 and line:find('^SOLID_VPHYSICS static prop with no vphysics model! %(.*%)$') then goto match end
		if c == 'U' and #line >= 18 and line:find('^Unable to remove .*!$') then goto match end
		if c == 'U' and #line >= 55 and line:find('^Unable to bind a key for command ".*" after %-?[0-9]+ attempt%(s%)%.$') then goto match end
		if c == 'm' and #line >= 42 and line:find('^m_face%->glyph%->bitmap%.width is 0 for ch:%-?[0-9]+ .*$') then goto match end
		if c == 'm' and #line >= 106 and line:find('^material .* has a normal map and %$basealphaenvmapmask%.  Must use %$normalmapalphaenvmapmask to get specular%.\n$') --[[ <-- extra newline ]] then goto match end
		if line == 'Unknown command "dimmer_clicked"' then goto match end
		if line == 'Unknown command: dimmer_clicked' then goto match end
		if line == 'hit surface has no samples' then goto match end
		goto nomatch
		::match::
		return true
	end
	::nomatch::
	return false
end

local dim_clutter = function (line)
	-- spammy messages that might still be useful in some cases
	if #line >= 18 then
		local c = line:sub(1, 1)
		if c == '\'' and #line >= 31 and line:find('^\'.*\' not present; not executing%.$') then goto match end
		if c == 'C' and #line >= 80 and line:find('^Can\'t use cheat cvar .* in multiplayer, unless the server has sv_cheats set to 1%.$') then goto match end
		if c == 'C' and #line >= 84 and line:find('^Can\'t change .* when playing, disconnect from the server or switch team to spectators$') then goto match end
		if c == 'F' and #line >= 55 and line:find('^FCVAR_CLIENTCMD_CAN_EXECUTE prevented running command: .*$') then goto match end
		if c == 'U' and #line >= 17 and line:find('^Unknown command: .*$') then goto match end
		if c == 'U' and #line >= 18 and line:find('^Unknown command ".*"$') then goto match end
		goto nomatch
		::match::
		return '\27[2m' .. line .. '\27[0m'
	end
	::nomatch::
	return nil
end

local bright = function (s) return string.format('\27[34;38;2;251;236;203m%s\27[0m', s) end

local parse_game_message = function (line)
	-- message output by "net_start"
	-- try connecting to rcon if credentials are set
	if #line >= 50 and line:find('^Network: IP .*, mode .*, dedicated .*, ports %-?[0-9]+ SV / %-?[0-9]+ CL$') then
		if  not rcon_connected
		and (   (os.getenv('CFGFS_RCON_HOST') and os.getenv('CFGFS_RCON_PORT'))
		     or  os.getenv('CFGFS_RCON_PASSWORD'))
		then
			ev_call(rcon, '')
		end
	end
	-- agpl backdoor (https://www.gnu.org/licenses/gpl-howto.en.html)
	if line:find('^.+ :  [\t ]*!cfgfs_agpl_source[\t ]*$') then
		cmd.say(agpl_source_url)
	end
	return false
end

local colorize_sourcemod_thing = function (line)
	-- colorize color codes so the \7 doesn't cause the terminal to blink
	-- line="\7FFD700[RTD]\1 Rolled \00732CD32Lucky Sandvich\1."
	if line:byte(1) == 7 then
		--line = line:gsub('\0[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]([^\1]*)\1', '%1')
		-- ^ where did this come from? i thought it couldn't print null bytes
		line = line:gsub('\7([0-9A-F][0-9A-F])([0-9A-F][0-9A-F])([0-9A-F][0-9A-F])([^\1]*)\1', function (r, g, b, s)
			r, g, b = tonumber('0x'..r), tonumber('0x'..g), tonumber('0x'..b)
			return string.format('\27[38;2;%d;%d;%dm%s\27[0m', r, g, b, s)
		end)
		return line
	end
	return false
end

local gco_unjumble = linereader(function (line)
	if line then
		return _game_console_output(line, true, true)
	end
end)
_game_console_output = function (line, complete, was_jumbled)
	if complete == true then
		if not was_jumbled then
			our_logfile:write(line, '\n')
			if fire_event('game_console_output', line) < 0 then
				return
			end
			if is_spam(line) then
				return
			end
			line = parse_game_message(line)
			    or colorize_sourcemod_thing(line)
			    or dim_clutter(line)
			    or line
			_println(line)
		else
			-- reconstructed jumbled line
			-- probably contains either
			-- * "help" command output for getting a cvar
			-- * echos from outside cfgfs

			our_logfile:write(line, '\n')

			-- 2 = dim
			printv('\27[2m', line, '\27[0m')
		end

		-- write out any jumbled line contents that might've been left
		--  in the buffer
		-- this might cause it to printed too early but that's not much
		--  different from how it would be printed in the in-game console
		return gco_unjumble(nil)
	elseif complete == false then
		if not (line == '\n' or line == '\r\n') then
			local contents, rest = line:match('^(.*)\x7f( \r?)$')
			if contents then
				-- cmd.echo() from lua
				-- (\x7f is added in cmd.c to mark echos so they
				--  can be identified here)

				our_logfile:write(contents, rest, '\n')
				-- preserve the trailing spac↑e from echo

				return _println(contents)
			else
				-- some other partial write

				-- this event is for the individual pieces of
				--  lines written in multiple parts
				if fire_event('game_console_output_jumbled', line) < 0 then
					return
				end

				return gco_unjumble(line)
			end
		else
			-- writing the newline
			return gco_unjumble(nil)
		end
	elseif complete == nil then
		-- line written to us manually
		our_logfile:write(line, '\n')
		return _println(line)
	else
		fatal('_game_console_output: bad value for "complete"')
	end
end

--------------------------------------------------------------------------------

do

local rcon_curr_id = 0
local rcon_lr = linereader(function (line)
	if not line then return end
	local cnt, data = fire_event('rcon_output', line, rcon_curr_id)
	local shall_log = true
	local shall_print = true
	if cnt < 0 then
		if type(data) == 'string' then
			shall_log = data:find('l')
			shall_print = data:find('p')
		else
			shall_log = false
			shall_print = false
		end
	end
	if shall_log then
		our_logfile:write(line, '\n')
	end
	if shall_print then
		_println(line)
	end
end)
_rcon_data = function (buf, id)
	if id then
		rcon_curr_id = id
	end
	return rcon_lr(buf)
end

rcon_connected = false
local rcon_connecting = false

local sess = nil
rcon = function (cfg)
	if sess then
		return _rcon_run_cfg(sess, cfg)
	else
		if not rcon_connecting then
			sess = _rcon_new(
			    (os.getenv('CFGFS_RCON_HOST') or 'localhost'),
			    (tonumber(os.getenv('CFGFS_RCON_PORT')) or 27015),
			    (os.getenv('CFGFS_RCON_PASSWORD') or nil))
			if not sess then
				return nil
			end
			rcon_connecting = true
		end
		wait_for_event('_rcon_auth')
		if sess then
			return _rcon_run_cfg(sess, cfg)
		else
			return nil
		end
	end
end

_rcon_status = function (s)
	if s == 'auth_ok' then
		rcon_connected = true
		rcon_connecting = false
		return fire_event('_rcon_auth')
	end
	if s == 'auth_fail' then
		rcon_connected = false
		rcon_connecting = false
		_rcon_delete(sess)
		sess = nil
		return fire_event('_rcon_auth')
	end
	if s == 'disconnect' then
		rcon_connected = false
		rcon_connecting = false
		if sess then
			_rcon_delete(sess)
			sess = nil
		end
		return
	end
end

end

--------------------------------------------------------------------------------

local startup_fired = false
add_listener('startup', function ()
	startup_fired = true
end)

local wait_cfgfs_mounted = function ()
	if not startup_fired then
		wait_for_event('startup')
	end
end

--------------------------------------------------------------------------------

-- non-blocking child processes

-- note: avoid firing the event with "nil" if nothing was written (cygfuse fix)
local last_data = nil
_message = function (name, data)
	if data then
		last_data = name
		return fire_event(name, data)
	else
		if last_data then
			last_data = nil
			return fire_event(name, data)
		end
	end
end

local get_channel = function (purpose)
	return string.format('%s.%08x',
	    purpose,
	    math.random(0x10000000, 0xffffffff))
end

local shellquote = function (s)
	return '\'' .. s:gsub('\'', '\'\\\'\'') .. '\''
end

sh = function (cmd, mode)
	wait_cfgfs_mounted()

	local chan = get_channel('cmd')

	mode = mode or 'r'
	local readable = not not mode:find('r')
	local writable = not not mode:find('w')

	local s =
		-- line 1
		'chan=' .. shellquote(chan) .. ';' ..
		-- line 2
		'CFGFS_PID_CHAN="${CFGFS_MOUNTPOINT}/message/${chan}.pid" ' ..
		    -- if the command is writable, then the 'w' flag is passed
		    --  to io.popen which makes stdin be a pipe (ok)
		    -- if it's not writable, then we should redirect stdin away
		    --  from the terminal so that the command doesn't try to
		    --  read from it
		    ((writable) and
		        '' or
		        '0<>/dev/null ') ..
		    -- if the command is readable, then redirect stdout to a
		    --  message channel we can read
		    -- if it's not readable and not writable, then io.popen('r')
		    --  was used which makes stdout a pipe. need to redirect it
		    --  to the terminal in this case
		    ((readable) and
		        '>"${CFGFS_MOUNTPOINT}/message/${chan}" ' or
		        ((writable) and
		            '' or
		            '>&2 ')) ..
		'sh -c ' ..
		    "'" ..
		        'echo $$ >"$CFGFS_PID_CHAN";' ..
		        'unset CFGFS_PID_CHAN;' ..
		    "'" ..
		    shellquote(cmd) ..
		    ';' ..
		-- line 3
		'echo $? >"${CFGFS_MOUNTPOINT}/message/${chan}.rv"'

	-- writable -> stdin is a pipe
	-- readable -> stdout is a pipe
	-- but we implement our own "readable" flag so only care about 'w' here
	local p = io.popen(s, ((writable) and 'w' or 'r'))

	local lr = nil
	local ar = nil

	local init_linereader = function ()
		if lr then return end
		lr = linereader(function (line)
			return fire_event(chan..'.line', line)
		end)
		spinoff(function ()
			for _, buf in wait_for_events(chan) do
				lr(buf)
				if buf == nil then break end
			end
			lr = nil
		end)
	end
	local init_allreader = function ()
		if ar then return end
		ar = allreader(function (data)
			return fire_event(chan..'.alldata', data)
		end)
		spinoff(function ()
			for _, buf in wait_for_events(chan) do
				ar(buf)
				if buf == nil then break end
			end
			ar = nil
		end)
	end

	local readall = function (chan)
		local rv = nil
		local ar = allreader(function (data)
			rv = data
		end)
		for _, buf in wait_for_events(chan) do
			ar(buf)
			if buf == nil then break end
		end
		return rv
	end

	local pid = nil
	local rv = nil

	spinoff(function ()
		pid = tonumber(readall(chan..'.pid'))
		if not pid then
			eprintln('sh(): failed to parse pid!')
			return
		end
		return fire_event(chan..'.started', pid)
	end)
	spinoff(function ()
		local rv = (tonumber(readall(chan..'.rv')) or -1)
		p:close()
		return fire_event(chan..'.exited', rv)
	end)

	local evname = wait_for_event({chan..'.started', chan..'.exited'})
	if evname == chan..'.exited' then
		return nil, rv
	end

	return {
		kill = function ()
			if not rv then
				os.execute('kill -TERM '..pid)
			end
		end,
		lines = function (self)
			if readable then
				if not rv then
					init_linereader()
					return function ()
						local evname, line = wait_for_event(chan..'.line')
						assert(evname)
						return line
					end
				else
					return error('already exited', 2)
				end
			else
				return error('not readable', 2)
			end
		end,
		read = function (self, what)
			if readable then
				if not rv then
					if what == 'a' then
						init_allreader()
						local evname, data = wait_for_event(chan..'.alldata')
						assert(evname)
						return data
					elseif what == 'l' then
						init_linereader()
						local evname, line = wait_for_event(chan..'.line')
						assert(evname)
						return line
					elseif what == 'n' then
						init_allreader()
						local evname, data = wait_for_event(chan..'.alldata')
						assert(evname)
						return tonumber(data)
					else
						return error('unsupported read', 2)
					end
				else
					return error('already exited', 2)
				end
			else
				return error('not readable', 2)
			end
		end,
		wait = function (self)
			if not rv then
				wait_for_event(chan..'.exited')
			end
			return rv
		end,
		write = function (self, data)
			if writable then
				if not rv then
					p:write(data)
				else
					return error('already exited', 2)
				end
			else
				return error('not writable', 2)
			end
		end,
	}
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

local update_notify_list = function ()
	local allpaths = {}
	for name in pairs(cfgfs.notify_cfgs) do
		table.insert(allpaths, '/' .. name)
	end
	for name in pairs(cfgfs.block_cfgs) do
		table.insert(allpaths, '/' .. name)
	end
	_notify_list_set(allpaths)
end

local create_nonexistent_notifys = function ()
	for name in pairs(cfgfs.notify_cfgs) do
		_ensure_cfg_exists(name)
	end
end

--------------------------------------------------------------------------------

-- local click_key_bound: declared near bind() where it's set

local con_logfile_main, con_logfile_tmp
if __linux__ then
	con_logfile_main = 'console.log'
	con_logfile_tmp = 'console_tmp.log'
else
	con_logfile_main = 'custom/!cfgfs/cfg/console.log'
	con_logfile_tmp = 'console.log'
end

local reinit_log = function ()
	cvar.con_logfile = con_logfile_tmp
	cvar.con_logfile = con_logfile_main
	cmd.echo('cfgfs: log file has been reinited')
end

local before_script_exec = function ()
	click_key_bound = false

	cmd.cfgfs_click = 'exec cfgfs/click'
	cmd.cfgfs_init = 'exec cfgfs/init'
	cmd.cfgfs_init_log = reinit_log

	cmd.cfgfs_license = 'exec cfgfs/license'

	cmd.cfgfs_restart = function ()
		cmd.echo "restarting..."
		cvar.con_logfile = con_logfile_tmp
		wait(0)
		_cfgfs_unmount()
		wait(150)
		cvar.con_logfile = con_logfile_main
		cmd.echo "restart failed?"
	end

	cmd.cfgfs_source = function () return cmd.echo(agpl_source_url) end
	cmd.release_all_keys = assert(release_all_keys)
end
local after_script_exec = function (path)
	update_notify_list()
	create_nonexistent_notifys()
	_reloader_add_watch(path)
	if __linux__ and not click_key_bound then
		eprintln('\awarning: no function key bound to "cfgfs_click" found!')
		eprintln(' why: one of the f1-f12 keys must be bound to "cfgfs_click" for delayed command execution to work')
		eprintln(' how: add a keybind like "bind(\'f11\', \'cfgfs_click\')" and re-save script.lua')
	end
end

-- reload and re-run script.lua
_reload_1 = function ()
	local path = (os.getenv('CFGFS_SCRIPT') or 'script.lua')
	local ok, err = loadfile(path)
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
	after_script_exec(path)

	if not ok then
		eprintln('\aerror: %s', err)
		eprintln('script.lua was reloaded with an error!')
		return true
	end

	println('script.lua reloaded successfully')

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

	fire_event('reload')

	collectgarbage()
end

-- initial run of script.lua is done and output isn't going in init.cfg anymore
_fire_startup = function ()
	-- probably crashed. need to reopen the log file and spit out init.cfg
	if os.getenv('CFGFS_RESTARTED') then
		reinit_log()
		_init()
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

-- booby trap require() so that reloader can watch source files loaded using it

do
	-- modname string -> state bool
	--   true = successfully watched
	--   false = tried to add watch but failed
	local watched_modules = {}

	-- un-cache loaded modules on reload so they're loaded from disk again
	-- note: this is needed even for modules that we've failed to watch,
	--  because without this we would never retry watching them (as loading
	--  cached modules using require() doesn't return the filesystem path)
	add_reset_callback(function ()
		for modname in pairs(watched_modules) do
			package.loaded[modname] = nil
			watched_modules[modname] = nil
		end
	end)

	-- modname string -> fspath string
	-- updated when a module is successfully loaded
	-- if a module fails to load, then this is used to find out the
	--  filesystem path for it
	local path_cache = {}

	local require_real = require
	require = function (modname)
		-- already loaded and watched this module
		if true == watched_modules[modname] then
			return require_real(modname)
		end
		local rv = {pcall(require_real, modname)}
		if rv[1] then
			local path = rv[3]
			if path and path:find('^%.*/.*%.[Ll][Uu][Aa]$') then
				path_cache[modname] = path
				watched_modules[modname] = _reloader_add_watch(path)
			end
			return select(2, table.unpack(rv))
		else
			-- require() or the module had an error

			-- if we had successfully loaded this module in the
			--  past, then we should know the filesystem path for it
			-- make reloader watch it so that correcting the error
			--  will retry loading the module
			if path_cache[modname] then
				watched_modules[modname] = _reloader_add_watch(path_cache[modname])
			end

			return error(rv[2], 2)
		end
	end
end

--------------------------------------------------------------------------------

local path = (os.getenv('CFGFS_SCRIPT') or 'script.lua')
local ok, err = loadfile(path)
if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end
local script = ok

before_script_exec()
local ok, err = xpcall(script, debug.traceback)
after_script_exec(path)

if not ok then
	eprintln('\aerror: %s', err)
	eprintln('failed to load script.lua!')
	return false
end

--------------------------------------------------------------------------------

collectgarbage()

return true
