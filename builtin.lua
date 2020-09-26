assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end

setmetatable(_G, {
	__index = function (self, k)
		println('warning: tried to access nonexistent variable %s', tostring(k))
		println('%s', debug.traceback(nil, 2))
	end,
})

collectgarbage('generational')
warn('@on')

println('cfgfs running with %s', _VERSION)

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

global = function (k, v)
	local t = rawget(_G, k)
	if t ~= nil then return t end
	rawset(_G, k, v)
	return v
end

--------------------------------------------------------------------------------

-- machinery to reset all state so that we can reload script.lua

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
		-- start fuse in single-threaded mode
		-- speedup of about 10%, but can cause lockups if something in
		--  script.lua accesses the cfgfs mount from outside (as the
		--  request won't be processed until the script returns)
		fuse_singlethread = true,

		init_on_reload = true,
		init_before_cfg = {},
		init_after_cfg = {
			['comfig/modules_run.cfg'] = true, -- stop setting fps_max to 1000!!!!!
		},

		intercept_blacklist = {
			['motd_entries.txt'] = true,
			['mtp.cfg'] = true,
			['server_blacklist.txt'] = true,
			['valve.rc'] = true,
		},
		intercept_blackhole = {
			['comfig/echo.cfg'] = true,
		},

		-- after script.lua is reloaded, "restore" these globals by
		--  firing the corresponding events with their values
		-- globals normally persist across reloads though
		restore_globals_on_reload = {
			['class'] = 'classchange',
			['slot'] = 'slotchange',
		},
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

cmd = setmetatable(make_resetable_table(), {
	__index = function (self, k)
		local f = function (...) return cmd(k, ...) end
		rawset(self, k, f)
		return f
	end,
	__newindex = function (self, k, v)
		if v == nil then
			-- is there really no "unalias" command???
			rawset(self, k, nil)
			return cmd.alias(k, '')
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

cvar = setmetatable({}, {
	__index = function (_, k)
		return cvar({k})[k]
	end,
	__newindex = function (_, k, v)
		return cmd(k, v)
	end,
	__call = function (_, t)
		if type(t) == 'string' then
			return cvar({t})[t]
		end
		if #t == 0 then
			for k, n in pairs(t) do
				cmd(k, v)
			end
			return
		end
		if gamedir then
			-- want: something to silence the output of "help"
			-- con_filter_enable doesn't seem to apply until later for some reason
			local f <close> = io.open(gamedir .. '/console.log')
			if not f then goto nolog end
			f:seek('end')
			for _, k in ipairs(t) do
				cmd('help', k)
			end
			yield()
			for l in f:lines() do
				local mk, mv = l:match('^"([^"]+)" = "([^"]*)')
				if mk then
					t[mk] = mv
				end
			end
		else
			goto nolog
		end
		do return t end
		::nolog::
		println('warning: couldn\'t open console log, reading cvars will not work')
		return t
	end,
})

--------------------------------------------------------------------------------

bind = function (key, cmd, cmd2)
	local n = key2num[key]
	if not n then
		println('warning: tried to bind unknown key "%s"', key)
		println('%s', debug.traceback(nil, 2))
		return
	end
	if not binds_down[key] then
		local cmd = _G.cmd
		cmd.alias('+key'..n, 'exec', 'cfgfs/keys/+'..n..'.cfg')
		cmd.alias('-key'..n, 'exec', 'cfgfs/keys/-'..n..'.cfg')
		cmd.bind(key, '+key'..n)
	end
	-- check if it's a +toggle
	if not cmd2 and type(cmd) ~= 'function' then
		cmd = tostring(cmd) or ''
		if cmd:find('^%+') then
			cmd2 = '-' .. cmd:sub(2)
		end
	end
	binds_down[key] = cmd
	binds_up[key] = cmd2
end

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

-- todo: be careful with where i am calling these
-- they need to be able to yield

add_listener = function (name, cb)
	events[name] = events[name] or {}
	table.insert(events[name], cb)
end

fire_event = function (name, v)
	if not events[name] then return end
	for _, cb in ipairs(events[name]) do
		cb(v)
	end
end

has_listeners = function (name)
	return not not events[name]
end

--------------------------------------------------------------------------------

local coros = make_resetable_table()
local co_id = 0

local main_co = nil
local main_fn = nil

local sym_main_ok = {}
local sym_timeout = {}

local timeouts = make_resetable_table()

yield = coroutine.yield

wait = function (ms)
	return yield(sym_timeout, ms)
end

wait2 = function (ms)
	local t = _ms()+ms
	_click(t)
	return yield(sym_timeout, t)
end

local resume_co = function (id, co, arg)
	local ok, rv, rv2 = coroutine.resume(co, arg)
	if not ok then
		println('error: %s', rv)
		if co == main_co then
			main_co = coroutine.create(main_fn)
		end
		return
	end
	if not id then
		if rv == sym_main_ok then
			return
		end
		main_co = coroutine.create(main_fn)
	end
	if rv == sym_timeout then
		if not id then
			id = co_id
			co_id = co_id + 1
		end
		return table.insert(timeouts, {
			at = rv2,
			co = co,
			id = id,
		})
	end
	if coroutine.status(co) == 'dead' then
		return
	end
	if rv ~= nil then
		println('warning: yielded unknown value %s', tostring(rv))
	end

	-- resume this next time i guess
	if not id then
		id = co_id
		co_id = co_id + 1
	end

	coros[id] = co
	return cmd.exec('cfgfs/resume/'..id)
end

local run_timeouts = function ()

	local ts = timeouts

	if #ts == 0 then return end

	local newts = {}
	timeouts = {}

	local now = nil
	for i = 1, #ts do
		now = now or _ms()
		local t = ts[i]
		if now >= t.at then
			resume_co(t.id, t.co)
			now = nil
		else
			table.insert(newts, t)
		end
	end
	if #newts > 0 then
		if #timeouts > 0 then
			table.insert(newts, table.unpack(timeouts))
		end
		timeouts = newts
	end
	-- ugly slow wrong code but luckily timeouts aren't very useful

end

--------------------------------------------------------------------------------

main_fn = function (path)
	local ok, rv = xpcall(function ()
		local path = path
		while true do
			exec_path(path)

			-- was yielded?
			if main_co ~= coroutine.running() then
				break
			end

			path = coroutine.yield(sym_main_ok)
		end
	end, debug.traceback)
	if not ok then
		println('error: %s', rv)
	end
end
-- ^ want: similar one for firing an event
-- event listeners need to be able to yield
-- make a loop like this one

main_co = coroutine.create(main_fn)

exec_path = function (path)

	-- keybind?
	local t = bindfilenames[path]
	if t then
		local b = binds_down
		if not t.pressed then
			b = binds_up
		end

		-- we know it's a function here
		return b[t.name]()
	end

	path = assert(path:after('/'))

	local m = path:after('cfgfs/')
	if m then
		path = m

		local m = path:between('resume/', '.cfg')
		if m then
			local id = tonumber(m)
			local co = id and coros[id]
			if not co then
				return println('warning: tried to resume nonexistent coroutine %s', m)
			end
			coros[id] = nil
			return resume_co(id, co)
		end

		local m = path:between('alias/', '.cfg')
		if m then
			local t = {}
			for arg in m:gmatch('[^/]+') do
				table.insert(t, arg)
			end
			local f = rawget(cmd, t[1])
			if f then
				if type(f) == 'function' then
					return f(select(2, table.unpack(t)))
				else
					return cfg(v)
				end
			else
				return println('warning: tried to exec nonexistent alias %s', m)
			end
		end

		local m = path:after('unmask_next/')
		if m then
			unmask_next[m] = 3
			return
		end

		println('warning: unknown cfgfs config "%s"', path)
	else
		local m = path:before('.cfg') or path

		if class2num[m] then
			fire_event('classchange', m)
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
	end

end

_get_contents = function (path)

	--cmd.echo('>>', path)

	-- keybind?
	local t = bindfilenames[path]
	if t then
		is_pressed[t.name] = t.pressed or nil

		local b = binds_down
		if not t.pressed then
			b = binds_up
		end

		local v = b[t.name]
		if v then
			if type(v) ~= 'function' then
				cfg(v)
				goto skip_main
			end
			goto do_main
		end

		goto skip_main

	end

	if path == '/cfgfs/buffer.cfg' then
		goto skip_main
	end
	if path == '/cfgfs/init.cfg' then
		_init()
		goto skip_main
	end

	::do_main::

	resume_co(nil, main_co, path)

	::skip_main::

	return run_timeouts()

end

--------------------------------------------------------------------------------

local bell = function () return io.stderr:write('\a') end

-- reload and re-run script.lua
_reload_1 = function ()
	local ok, err = loadfile('./script.lua')
	if not ok then
		bell()
		println('error: %s', err)
		println('failed to reload script.lua!')
		return false
	end
	local script = ok

	do_reset()

	local ok, err = xpcall(script, debug.traceback)
	if not ok then
		bell()
		println('error: %s', err)
		println('script.lua was reloaded with an error!')
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
			local ok, err = xpcall(function () return fire_event(evt, global(name)) end, debug.traceback)
			if not ok then
				println('error: %s', err)
			end
		end
	end

	local ok, err = xpcall(function () return fire_event('reload') end, debug.traceback)
	if not ok then
		println('error: %s', err)
	end
end

_fire_startup = function ()
	local ok, err = xpcall(function () return fire_event('startup') end, debug.traceback)
	if not ok then
		println('error: %s', err)
	end
end

--------------------------------------------------------------------------------

local ok, err = loadfile('./script.lua')
if not ok then
	bell()
	println('error: %s', err)
	println('failed to load script.lua!')
	return false
end
local script = ok

local ok, err = xpcall(script, debug.traceback)
if not ok then
	bell()
	println('error: %s', err)
	println('failed to load script.lua!')
	return false
end

--------------------------------------------------------------------------------

collectgarbage()

return true
