--[[

replays events for testing/benchmarking/PGO
% lua vcr_replay.log vcr/mygame.log
to produce logs, build cfgfs like "make -B VCR=1"

things that currently aren't recorded but should be:
- contents of script.lua on load/reload -- just need to do it
- terminal signals/eof -- starting to regret this

]]

local cfgfs_command = 'perf record ./cfgfs'
local cfgfs_command = 'doas valgrind ./cfgfs -o allow_other'
local cfgfs_command = 'perf stat ./cfgfs'

local fcntl  = require 'posix.fcntl'
local stat   = require 'posix.sys.stat'
local unistd = require 'posix.unistd'

local libvcr = assert(require 'libvcr', 'failed to load libvcr (do "make libvcr.so" first)')

local exec_ok = os.execute
if _VERSION == 'Lua 5.1' then
	exec_ok = function (...)
		return (os.execute(...) == 0)
	end
end

--------------------------------------------------------------------------------

local _load_cache = {}
local load_events = function (file)
	if _load_cache[file] then
		return _load_cache[file]
	end
	local events = {}
	_G.event = function (t)
		if t.path then t.path = 'mnt' .. t.path end
		if t.what == 'game_console_output' then
			if t.text:find('^Model \'[^\']+\' doesn\'t have attachment ') then return end
			if t.text:find('^EmitSound: pitch out of bounds = ') then return end
			if t.text:find('^(.+) killed (.+) with (.+)%.$') then return end
		end
		return table.insert(events, t)
	end
	dofile(file)
	_G.event = nil
	-- it is likely that they're already in chronological order
	--table.sort(events, function (t1, t2)
	--	return t1.timestamp < t2.timestamp
	--end)
	if #events == 0 then
		return nil, file..': no events recorded'
	end
	_load_cache[file] = events
	return events
end
local events = load_events(arg[1] or './vcr.log')

--------------------------------------------------------------------------------

local console = nil

local fds = nil

local actions = {
	getattr = function (t)
		local rv = stat.lstat(t.path)
		if rv then
			return check(t.rv == 0, 'getattr unexpectedly succeeded')
		else
			return check(t.rv ~= 0, 'getattr unexpectedly failed')
		end
	end,
	open = function (t)
		local fd = fcntl.open(t.path, fcntl.O_RDONLY)
		if t.rv == 0 then
			check(fds[t.fd] == nil, 'open: fd already opened')
			check(fd ~= nil, 'open unexpectedly failed')
			fds[t.fd] = fd
		else
			if fd ~= nil then
				check_fail('open unexpectedly succeeded')
				return unistd.close(fd)
			end
		end
	end,
	release = function (t)
		local t_fd = t.fd
		local l_fd = fds[t_fd]
		if l_fd then
			local rv = unistd.close(l_fd)
			if rv then
				fds[t_fd] = nil
				return
			else
				return check_fail('release: close failed')
			end
		else
			return check_fail('release: fd not opened')
		end
	end,
	read = function (t)
		if fds[t.fd] then
			local s = unistd.read(fds[t.fd], t.size)
			check(s ~= nil, 'read failed')
			check(s == t.data, 'read wrong data')
			-- `t.offset` is just how much has been read from it previously
			-- it's automatically correct here without doing anything
		else
			return check_fail('read: fd not opened')
		end
	end,
	game_console_output = function (t)
		return console:write(t.text..'\n')
	end,
	cli_input = function (t)
		return libvcr.ttype(tty_fd, t.text..'\n')
		-- i hope this works because im too sick of it to test it any further
		-- need to implement signals/^D too
	end,
}
if false then -- disable checking. it's not that slow though
actions = {
	getattr = function (t)
		return stat.lstat(t.path)
	end,
	open = function (t)
		local fd = fcntl.open(t.path, fcntl.O_RDONLY)
		if t.rv == 0 then
			fds[t.fd] = fd
		elseif fd ~= nil then
			return unistd.close(fd)
		end
	end,
	release = function (t)
		local t_fd = t.fd
		local l_fd = fds[t_fd]
		if l_fd then
			local rv = unistd.close(l_fd)
			if rv then
				fds[t_fd] = nil
			end
		end
	end,
	read = function (t)
		return unistd.read(fds[t.fd], t.size)
	end,
	game_console_output = function (t)
		return console:write(t.text..'\n')
	end,
	cli_input = function (t)
		return libvcr.ttype(tty_fd, t.text..'\n')
	end,
	cli_eof = function (t)
		-- how to do this?
	end,
}
end

--------------------------------------------------------------------------------

local unmount_all_cfgfs = function ()
	if not exec_ok([[
	set -e
	mount -t fuse.cfgfs | grep -Po ' on \K(.+)(?= type )' | xargs -rd'\n' fusermount -u
	! [ -L mnt ] || rm -f mnt
	! [ ! -e mnt ] || mkdir mnt
	]]) then
		error('failed to unmount cfgfs mounts')
	end
end

local child_start = function ()
	if not exec_ok([[
	( grep -qm1 'a' /dev/tty && exec ]]..cfgfs_command..[[ ./mnt >/dev/null ) &
	]]) then
		error('failed to start cfgfs')
	end
	libvcr.ttype(1, 'a\n')
	libvcr.wait_cfgfs_mounted('mnt')
end

--------------------------------------------------------------------------------

local e2str = function (t)
	local t2 = {}
	for k, v in pairs(t) do
		t2[#t2+1] = string.format('  %s=%q', k, v)
	end
	return table.concat(t2, '\n')
end

local errors = 0

local g_event = nil

check_fail = function (fmt, ...)
	errors = (errors + 1)
	print(string.format('check failed: ' .. fmt, ...))
	print(string.format('failing event:\n%s\n', e2str(g_event)))
end
check = function (cond, fmt, ...)
	if cond then return end
	return check_fail(fmt, ...)
end

--------------------------------------------------------------------------------

if #arg == 0 then
	io.stderr:write('usage: vcr_replay.lua <logfile> ...\n')
	os.exit(1)
end

if not exec_ok([[
set -e
[ ! -e perf.stat ] || rm perf.stat
[ ! -e perf.stat ] || rm console.log
ln -sf /tmp/vcr_console.log console.log
]]) then os.exit(1) end

console = io.open('./console.log', 'w')
console:setvbuf('full')

for _, file in ipairs(arg) do
	local events = assert(load_events(file))
	fds = {}
	errors = 0
	unmount_all_cfgfs()
	child_start()
	local prev = ''
	for i, t in ipairs(events) do
		g_event = t
		if (prev == 'game_console_output' and t.what ~= 'game_console_output') then
			console:flush()
		end
		actions[t.what](t)
		prev = t.what
	end
	os.execute('exec fusermount -u mnt')
	local unclosed = 0
	for _ in pairs(fds) do unclosed = (unclosed + 1) end
	print(string.format('%d events %d errors %d fds unclosed',
		#events,
		errors,
		unclosed))
end

if not exec_ok([[
rm -f console.log /tmp/vcr_console.log
touch console.log
]]) then os.exit(1) end
