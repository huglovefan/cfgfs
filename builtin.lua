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
		-- ~10% faster but can cause lockups if script.lua accesses the cfgfs mount from outside
		-- (requests can't be processed until the script returns)
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

		-- prevent the game from seeing these configs
		intercept_blackhole = {
			-- ['example.cfg'] = true,
		},

		-- after script.lua is reloaded, "restore" these globals by firing the corresponding events with their values
		-- (globals normally persist across reloads though)
		restore_globals_on_reload = {
			['class'] = 'classchange',
			['slot'] = 'slotchange',
		},

		-- avoid using alias command for games that don't have it (fof)
		compat_noalias = false,

		-- game_window_title = '',
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

-- how many times script.lua has been reloaded
-- (0 on the initial load, 1 on the first reload ...)
reload_count = global('reload_count', 0)

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
-- edit: what can we do now that logtail calls a function here? line listener?
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
			-- is there no way to do this silently?
			-- con_filter_enable doesn't apply until later for some reason
		end
		yield()
		for l in f:lines() do
			local mk, mv = l:match('"([^"]+)" = "([^"]*)"')
			if mk then
				t[mk] = mv
			end
		end
		return t
		-- note: the match pattern does not use "^"
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

wait2 = function (ms) -- todo: rename
	local t = _ms()+ms
	_click_after(ms)
	return yield(sym_timeout, t)
end

-- id: numeric id in the `coros` table (nil for the "main" coroutine)
-- co: the coroutine to resume
-- arg: additional argument to resume with
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

-- mostly untested
set_timeout = function (fn, ms)
	local cancelled = false
	spinoff(function ()
		wait2(ms)
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
			wait2(ms)
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
			for _, t in ipairs(timeouts) do
				table.insert(newts, t)
			end
		end
		timeouts = newts
	end
	-- ugly slow wrong code but luckily timeouts aren't very useful
	-- (what was wrong about it?)
	-- was it due to emptying the timeouts list first
	-- cfgfs can't cancel timeouts so that shouldn't be a problem

end

--------------------------------------------------------------------------------

-- these happen after normal binds

local key_listeners = make_resetable_table()
add_key_listener = function (key, f)
	key_listeners[key] = (key_listeners[key] or {})
	key_listeners[key][f] = true
end
remove_key_listener = function (key, f)
	if key_listeners[key] then
		key_listeners[key][f] = nil
		if next(key_listeners[key]) == nil then
			key_listeners[key] = nil
		end
	end
end
local fire_key_event = function (key, state)
	local t = key_listeners[key]
	if not t then return end
	for fn in pairs(key_listeners[key]) do
		resume_co(nil, main_co, function () return fn(state) end)
	end
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
		end

		eprintln('warning: unknown cfgfs config "%s"', path)
	else
		local m = path:before('.cfg') or path

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
				fire_key_event(t.name, true)
				goto skip_main
			end
			fire_key_event(t.name, true) -- XXX this is in the wrong place
			goto do_main
		elseif t.type == 'up' then
			is_pressed[t.name] = nil
			local v = binds_up[t.name]
			if not v then goto skip_main end
			if type(v) ~= 'function' then
				cfg(v)
				fire_key_event(t.name, false)
				goto skip_main
			end
			fire_key_event(t.name, false) -- XXX this too
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
			fire_key_event(t.name, pressed)
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
			fire_key_event(t.name, true)
			is_pressed[t.name] = nil
			if vu ~= nil then
				if type(vu) ~= 'function' then
					cfg(vu)
				else
					resume_co(nil, main_co, string.format('/cfgfs/keys/-%d.cfg', key2num[t.name]))
				end
			end
			fire_key_event(t.name, false)
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
	if path == '/config.cfg' then
		cmd.echo('')
		cmd.echo('cfgfs is free software released under the terms of the GNU AGPLv3 license.')
		cmd.echo('Type `cfgfs_license\' for details.')
		cmd.echo('')
		goto do_main
	end
	if path == '/cfgfs/license.cfg' then
		local f <close> = assert(io.open('LICENSE', 'r'))
		for line in f:lines() do
			cmd.echo(line)
		end
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

local is_active = (_get_attention() == cfgfs.game_window_title)
_attention = function (title)
	local was_active = is_active
	is_active = (title ~= nil and title == cfgfs.game_window_title)
	if is_active ~= was_active then
		local ok, err = xpcall(function () return fire_event('attention', is_active) end, debug.traceback)
		if not ok then
			eprintln('error: %s', err)
		end
	end
end
is_game_window_active = function ()
	return is_active
end

--------------------------------------------------------------------------------

local repl_fn = function (code)
	local fn, err1 = load(code, 'input')
	if fn then
		return fn
	end
	-- note: ",1" at the end is so that we can get the values after the first nil
	-- (is there no better way?)
	-- edit: http://lua-users.org/lists/lua-l/2020-10/msg00211.html
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
		if line == 'cfgfs_license' then
			local f <close> = assert(io.open('LICENSE', 'r'))
			for line in f:lines() do
				println(line)
			end
			return
		end
		return cfg(line)
	end
end

_game_console_output = function (line)
	println('%s', line)
	local ok, err = xpcall(function () return fire_event('game_console_output', line) end, debug.traceback)
	if not ok then
		eprintln('error: %s', err)
	end
	-- agpl backdoor (https://www.gnu.org/licenses/gpl-howto.en.html)
	-- it doesn't work if you say it yourself
	if line:find(':[\t ]*!cfgfs_agpl_source[\t ]*$') then
		cmd.say(agpl_source_url)
	end
end
assert((type(agpl_source_url) == 'string' and #agpl_source_url > 0), 'invalid agpl_source_url')

cmd.cfgfs_license = 'exec cfgfs/license'
cmd.cfgfs_source = function () cmd.echo(agpl_source_url) end

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
	reload_count = (reload_count + 1)

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
