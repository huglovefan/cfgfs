assert(_print)
println = function (fmt, ...) return _print(string.format(fmt, ...)) end
assert(_eprint)
eprintln = function (fmt, ...) return _eprint(string.format(fmt, ...)) end

setmetatable(_G, {
	__index = function (self, k)
		eprintln('warning: tried to access nonexistent variable %s', tostring(k))
		eprintln('%s', debug.traceback(nil, 2))
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

global = function (k, v2)
	local v1 = rawget(_G, k)
	if v1 ~= nil and not (type(v1) == 'table' and type(v2) == 'table' and v1._v ~= v2._v) then
		return v1
	end
	rawset(_G, k, v2)
	return v2
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
		-- run fuse in single-threaded mode
		-- speedup of about 10%, but can cause lockups if something in
		--  script.lua accesses the cfgfs mount from outside (as the
		--  request won't be processed until the script returns)
		fuse_singlethread = true,

		init_on_reload = true,
		-- there's no init_on_load because that would run before game configs
		-- set something in init_after_cfg instead

		init_before_cfg = {},
		init_after_cfg = {
			['comfig/modules_run.cfg'] = true, -- stop setting fps_max to 1000!!!!!
		},

		-- things not in cfg format
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

		-- avoid using alias command for games that don't have it (fof)
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

-- TODO compat_noalias -- allow making functions in this table
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

-- want: something to silence the output of "help"
-- con_filter_enable doesn't seem to apply until later for some reason

--[[
run_capturing_output = function (fn)
	local f <close> = assert(io.open((gamedir or '.') .. '/console.log', 'r'))
	f:seek('end')
	local id = string.format('-capture%d', math.random(0xffffffff))
	cmd('echo', id..'a')
	fn()
	cmd('echo', id..'b')
	yield()
	local on = false
	local lines = {}
	for l in f:lines() do
		if on then
			if l ~= id..'b ' then -- echo puts a space after it
				lines[#lines+1] = l
			else
				on = false
			end
		else
			if l == id..'a ' then
				on = true
			end
		end
	end
	return lines
end
-- fixme: merge these
-- probably use the old one (no math.random = simpler and better for vcr)
]]

cvar = setmetatable({}, {
	__index = function (_, k)
		return cvar({k})[k]
	end,
	__newindex = function (_, k, v)
		-- ignore nil i guess
		if v ~= nil then return cmd(k, v) end
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
		local f <close> = assert(io.open((gamedir or '.') .. '/console.log', 'r'))
		f:seek('end')
		for _, k in ipairs(t) do
			cmd('help', k)
		end
		yield()
		for l in f:lines() do
			local mk, mv = l:match('"([^"]+)" = "([^"]*)')
			if mk then
				t[mk] = mv
			end
			-- note: match pattern does not use "^"
			-- due to mysteries, console output in source sometimes
			--  comes up jumbled with the order of words messed up
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
			-- the `"name" = "value"` part is luckily always intact
		end
		return t
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
			cmd.alias('+key'..n, 'exec', 'cfgfs/keys/+'..n..'.cfg')
			cmd.alias('-key'..n, 'exec', 'cfgfs/keys/-'..n..'.cfg')
			cmd.bind(key, '+key'..n)
		end
	else
		local cmd = _G.cmd
		if cmd2 then
			cmd.bind(key, '+jlook;exec cfgfs/keys/^'..n..'.cfg//')
		else
			cmd.bind(key, 'exec cfgfs/keys/@'..n..'.cfg')
		end
		-- (if cmd2 then ...)
		-- the bind string needs to start with + to run something on key release too
		-- +jlook is there because it does nothing and starts with +
		-- "//" starts a comment to ignore excess arguments
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

--[[

these should be the events:

classchange   a class config was executed
slotchange*   weapon slot changed
startup       fired after script.lua is executed for the first time
reload        fired after script.lua is reloaded due to being modified

more? are these even right?

]]

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

--[[wait = function (ms) -- what was this for
	return yield(sym_timeout, ms)
end]]

wait2 = function (ms)
	local t = _ms()+ms
	_click(t)
	return yield(sym_timeout, t)
end

local resume_co = function (id, co, arg)
	local ok, rv, rv2 = coroutine.resume(co, arg)
	if not ok then
		eprintln('error: %s', rv)
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
		eprintln('warning: yielded unknown value %s', tostring(rv))
	end

	-- resume this next time i guess

	if not id then
		id = co_id
		co_id = co_id + 1
	end

	coros[id] = co
	return cmd.exec('cfgfs/resume/'..id)
end

-- adds a new coroutine to the machine
spinoff = function (fn, ...)
	local id = co_id
	co_id = co_id + 1
	return resume_co(id, coroutine.create(fn), ...)
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
			if type(path) == 'string' then
				exec_path(path)
			elseif type(path) == 'function' then
				path()
			else
				return error('main_fn: path has wrong type')
			end

			-- was yielded?
			if main_co ~= coroutine.running() then
				break
			end

			path = coroutine.yield(sym_main_ok)
		end
	end, debug.traceback)
	if not ok then
		eprintln('error: %s', rv)
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
		if t.type == 'down' then
			return binds_down[t.name]()
		elseif t.type == 'up' then
			return binds_up[t.name]()
		else
			return error('exec_path: wrong type of bind')
		end
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
				return eprintln('warning: tried to resume nonexistent coroutine %s', m)
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
				return eprintln('warning: tried to exec nonexistent alias %s', m)
			end
		end

		local m = path:after('unmask_next/')
		if m then
			unmask_next[m] = 3
			return
			-- the next 3 times the FUSE code gets a request about this config file,
			--  we'll report that we don't have it so that source will find and exec the real one instead
			-- has to be 3 because source stats configs twice before opening them for reading
		end

		eprintln('warning: unknown cfgfs config "%s"', path)
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
		if t.type == 'down' then
			is_pressed[t.name] = true
			local v = binds_down[t.name]
			if not v then goto skip_main end
			if type(v) ~= 'function' then
				cfg(v)
				goto skip_main
			end
			goto do_main
		elseif t.type == 'up' then
			is_pressed[t.name] = nil
			local v = binds_up[t.name]
			if not v then goto skip_main end
			if type(v) ~= 'function' then
				cfg(v)
				goto skip_main
			end
			goto do_main
		elseif t.type == 'toggle' then
			local pressed = (not is_pressed[t.name])
			is_pressed[t.name] = pressed or nil
			local b = binds_down
			if not pressed then
				b = binds_up
			end
			local v = b[t.name]
			if not v then goto skip_main end
			if type(v) ~= 'function' then
				cfg(v)
			else
				if pressed then
					resume_co(nil, main_co, string.format('/cfgfs/keys/+%d.cfg', key2num[t.name]))
				else
					resume_co(nil, main_co, string.format('/cfgfs/keys/-%d.cfg', key2num[t.name]))
				end
			end
			goto skip_main
		elseif t.type == 'once' then
			local vd = binds_down[t.name]
			local vu = binds_up[t.name]
			if vd ~= nil then
				if type(vd) ~= 'function' then
					cfg(vd)
				else
					is_pressed[t.name] = true
					resume_co(nil, main_co, string.format('/cfgfs/keys/+%d.cfg', key2num[t.name]))
				end
			end
			is_pressed[t.name] = nil
			if vu ~= nil then
				if type(vu) ~= 'function' then
					cfg(vu)
				else
					resume_co(nil, main_co, string.format('/cfgfs/keys/-%d.cfg', key2num[t.name]))
				end
			end
			goto skip_main
		else
			return error('unknown bind type')
		end
		-- ^ epic copypasta ^
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

-- relief for buggy toggle keys
release_all_keys = function ()
	for key in pairs(is_pressed) do
		is_pressed[key] = nil
		local vu = binds_up[key]
		if vu ~= nil then
			if type(vu) ~= 'function' then
				cfg(vu)
			else
				cmd.exec(string.format('cfgfs/keys/-%d.cfg', key2num[key]))
				-- we're (probably) in the coroutine so can't resume it here, need to use exec
			end
		end
	end
end

--------------------------------------------------------------------------------

local repl_fn = function (code)
	local fn, err1 = load(code, 'input')
	if fn then
		return fn
	end
	-- note: ",1" at the end is so that we can get the values after the first nil
	-- (is there no better way?)
	local fn = load('return '..code..',1', 'input')
	if fn then
		return function ()
			local t = {fn()}
			for i = 1, #t do
				t[i] = tostring(t[i]) or 'nil'
			end
			t[#t] = nil
			return println('%s', table.concat(t, '\t'))
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
		return resume_co(nil, main_co, fn)
	else
		return cfg(line)
	end
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
			local ok, err = xpcall(function () return fire_event(evt, global(name)) end, debug.traceback)
			if not ok then
				eprintln('error: %s', err)
			end
		end
	end

	local ok, err = xpcall(function () return fire_event('reload') end, debug.traceback)
	if not ok then
		eprintln('error: %s', err)
	end
end

_fire_startup = function ()
	local ok, err = xpcall(function () return fire_event('startup') end, debug.traceback)
	if not ok then
		eprintln('error: %s', err)
	end
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
