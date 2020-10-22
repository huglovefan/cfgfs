--[[

replays events for testing/benchmarking/PGO
% lua vcr_replay.log vcr/mygame.log
to produce logs, build cfgfs like "make -B VCR=1"

things that currently aren't recorded but should be:
- contents of script.lua on load/reload -- just need to do it
- terminal signals/eof -- starting to regret this

]]

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

local events = {}
event = function (t)
	table.insert(events, t)
end
dofile(arg[1] or './vcr.log')
table.sort(events, function (t1, t2)
	return t1.timestamp < t2.timestamp
end)
event = nil
if #events == 0 then
	io.stderr:write('vcr_replay: no events recorded\n')
	os.exit(1)
end

--------------------------------------------------------------------------------

local console = io.open('./console.log', 'w')
console:setvbuf('full')

local fds = {}

local actions = {
	--[[
	vcr_event {
		vcr_add_string("what", "getattr");
		vcr_add_string("path", path);
		//vcr_add_integer("fd", (fi != NULL) ? (long long)fi->fh : -1);
		vcr_add_integer("rv", (long long)rv);
		vcr_add_double("timestamp", tm);
	}
	]]
	getattr = function (t)
		local st = stat.lstat(t.path)
		if st == nil then
			assert(t.rv ~= 0, 'getattr unexpectedly failed')
		else
			assert(t.rv == 0, 'getattr unexpectedly succeeded')
		end
	end,
	--[[
	vcr_event {
		vcr_add_string("what", "open");
		vcr_add_string("path", path);
		if (rv == 0) {
			vcr_add_integer("fd", (long long)fi->fh);
		}
		vcr_add_integer("rv", (long long)rv);
		vcr_add_double("timestamp", tm);
	}
	]]
	open = function (t)
		if t.fd then assert(fds[t.fd] == nil, 'open: fd already opened') end

		local fd = fcntl.open(t.path, fcntl.O_RDONLY)
		if t.rv == 0 then
			assert(fd ~= nil, 'open unexpectedly failed')
			fds[t.fd] = fd
		else
			assert(fd == nil, 'open unexpectedly succeeded')
			if fd then unistd.close(fd) end
		end
	end,
	--[[
	vcr_event {
		vcr_add_string("what", "release");
		vcr_add_integer("fd", (long long)fi->fh);
		vcr_add_double("timestamp", tm);
	}
	]]
	release = function (t)
		t.rv = 0
		assert(t.fd >= 1, 'release: invalid fd')
		assert(fds[t.fd] ~= nil, 'release: unknown fd')
		assert(fds[t.fd] >= 0)
		local rv = unistd.close(fds[t.fd])
		assert(rv == 0)
		fds[t.fd] = nil
	end,
	--[[
	vcr_event {
		vcr_add_string("what", "read");
		vcr_add_integer("size", (long long)size);
		vcr_add_integer("offset", (long long)offset);
		vcr_add_integer("fd", (long long)fi->fh);
		vcr_add_integer("rv", (long long)rv);
		vcr_add_string("data", buf);
		vcr_add_double("timestamp", tm);
	}
	]]
	read = function (t)
		local s = unistd.read(fds[t.fd], t.size)
		assert(s ~= nil, 'read failed')
		assert(s == t.data, 'read wrong data')
		-- `t.offset` is just how much has been read from it previously
		-- it's automatically correct here without doing anything
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
	( grep -qm1 'a' /dev/tty && exec perf stat ./cfgfs ./mnt >/dev/null ) &
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

local stats = {}
local tick = function (k) stats[k] = (stats[k] or 0) + 1 end
local tick_zero = function (k) stats[k] = 0 end
tick_zero('asserts')
tick_zero('ignored')

local g_event = nil

assert = function (x, s)
	if not x then
		tick('asserts')
		print(string.format('assertion failed: %s', s or '(nil)'))
		print(string.format('failing event:\n%s\n', e2str(g_event)))
	end
end

--------------------------------------------------------------------------------

unmount_all_cfgfs()
child_start()

local now = 0
local ignored = 0
local prev = nil
for i, t in ipairs(events) do
	if t.path then t.path = 'mnt' .. t.path end
	g_event = t
	if prev == 'log' and t.what ~= 'log' then console:flush() end

	if t.what == 'log' then
		if t.text:find('^Model \'[^\']+\' doesn\'t have attachment ') then goto next end
		if t.text:find('^EmitSound: pitch out of bounds = ') then goto next end
		if t.text:find('^(.+) killed (.+) with (.+)%.$') then goto next end
	end

	actions[t.what](t)
	prev = t.what
	::next::
end

os.execute('fusermount -u mnt')

--------------------------------------------------------------------------------

local unclosed = 0
for _ in pairs(fds) do unclosed = (unclosed + 1) end

print(string.format('%d events %d ignored %d errors %d fds unclosed', #events, stats['ignored'], stats['asserts'], unclosed))
