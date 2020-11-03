-- wip in progress
-- want an accurate tf2 simulation for testing

local fcntl  = require 'posix.fcntl'
local stat   = require 'posix.sys.stat'
local unistd = require 'posix.unistd'

local lpeg = require 'lpeg'

local lfs = require 'lfs'

--------------------------------------------------------------------------------

local cfg_grammar = lpeg.P {
	'script',

	space = lpeg.R'\0 '
	      - lpeg.P'\n';

	separator = lpeg.P';';

	comment = lpeg.P'//'
	        * lpeg.V'comment_char'^0;
	comment_char = lpeg.P(1)
	             - lpeg.P'\n';

	word = lpeg.C(lpeg.V'word_char'^1);
	word_char = lpeg.P(1)
	          - lpeg.S' \n";'
	          - lpeg.P'//';

	quoted = lpeg.P'"'
	       * lpeg.C(lpeg.V'quoted_char'^0)
	       * lpeg.P'"'^-1; -- closing quote is optional
	quoted_char = lpeg.P(1)
	            - lpeg.S'"\n';

	command = ( lpeg.V'command_part'
	          * lpeg.V'space'^0 )^1;
	command_part = lpeg.V'word'
	             + lpeg.V'quoted';

	anything = lpeg.P'\n'
	         + lpeg.V'space'
	         + lpeg.V'separator'
	         + lpeg.V'comment'
	         + lpeg.Ct(lpeg.V'command');

	script = lpeg.Ct( lpeg.V'anything'^1 )
	       * lpeg.P(-1);
}

local cfg_parse_script = function (text)
	return (cfg_grammar:match(text) or {})
end

--------------------------------------------------------------------------------

local cvar = {}
setmetatable(cvar, cvar)
cvar.new = function (mt)
	return setmetatable({
		value = '',
	}, mt)
end
cvar.toboolean = function (self)
	return (self:tointeger() ~= 0)
end
cvar.tofloat = function (self)
	return tonumber(self.value)
end
cvar.tointeger = function (self)
	return math.floor(self:tointeger())
end

--------------------------------------------------------------------------------

local aliases = {}
local commands = {}
local variables = {}

--------------------------------------------------------------------------------

local println = function (fmt, ...)
	return io.write(string.format(fmt, ...),'\n')
end
local eprintln = function (fmt, ...)
	return io.stderr:write(string.format(fmt, ...),'\n')
end

--------------------------------------------------------------------------------

local gamedir = string.format('%s/.local/share/Steam/steamapps/common/Team Fortress 2/tf', os.getenv('HOME'))

local cfg_search_dirs = {}
for moddir in lfs.dir(string.format('%s/custom', gamedir)) do
	moddir = string.format('%s/custom/%s', gamedir, moddir)
	if lfs.attributes(moddir, 'mode') == 'directory' then
		table.insert(cfg_search_dirs, string.format('%s/cfg', moddir))
	end
end
table.sort(cfg_search_dirs)
table.insert(cfg_search_dirs, string.format('%s/cfg', gamedir))

local cfg_eval_command
local cfg_eval_script

local console_logfile <close> = assert(io.open(gamedir..'/console.log', 'a'))

cfg_eval_command = function (cmd)

	if not (cmd and cmd[1]) then return end

	local alias = aliases[cmd[1]]
	if alias then
		return cfg_eval_script(alias)
	end

	local command = commands[cmd[1]]
	if command then
		return command(cmd)
	end

	println('%s: command not found', cmd[1])

end

cfg_eval_script = function (text)

	for _, cmd in ipairs(cfg_parse_script(text)) do
		println('+ "%s"', table.concat(cmd, '" "'))
		cfg_eval_command(cmd)
	end

end

--------------------------------------------------------------------------------

commands.alias = function (cmd)
	if #cmd == 1 then
		for name, text in pairs(aliases) do
			println('%s = %s', name, text)
		end
	elseif #cmd == 2 then
		local name = cmd[2]
		if aliases[name] then
			println('%s = %s', name, aliases[name])
		else
			println('%s: no such alias')
		end
	elseif #cmd >= 3 then
		local name = cmd[2]
		local value = {}
		for i = 3, #cmd do
			table.insert(value, string.format('"%s"', cmd[i]))
		end
		aliases[name] = table.concat(value, ' ')
	else
		println('usage: alias <command> [newvalue]')
	end
end

--------------------------------------------------------------------------------

commands.bind = function (cmd)
end

--------------------------------------------------------------------------------

commands.echo = function (cmd)
	local t = {}
	for i = 2, #cmd do
		table.insert(t, cmd[i])
	end
	local s = table.concat(t, ' ')
	println('%s', s)
	if console_logfile then
		console_logfile:write(s, '\n')
		console_logfile:flush()
	end
end

--------------------------------------------------------------------------------

-- so the dance tf2 does is
-- 1. open -> stat -> close (determine if it exists)
-- 2. open -> stat -> close (just because)
-- 3. open -> stat -> read -> close
-- can see the exact events if caching is disabled in fuse (cfgfs_init in main.c)

-- if the config doesn't exist, it gives up on the first stat
-- if the config exists outside cfgfs, we get the 3 stat calls but not the read

local exec_lookup_cfg = function (name)
	name = name:lower()
	if not name:find('%.cfg$') then
		name = (name .. '.cfg')
	end
	for _, dir in ipairs(cfg_search_dirs) do
		local path = string.format('%s/%s', dir, name)
		local f = io.open(path, 'r')
		if f then
			assert(lfs.attributes(path)) -- unknown what happens if this fails
			return f
		else
			-- it reads the directory to look for case-insensitive matches
			-- (not implemented, assume they don't exist)
			local dirpath = path:gsub('[^/]+$', '')
			pcall(function () for _ in lfs.dir(dirpath) do end end)
		end
	end
	return nil
end

commands.exec = function (cmd)
	if #cmd ~= 2 then
		println('usage: exec <file>')
		return
	end

	local name = cmd[2]

	local f = exec_lookup_cfg(name)
	if not f then
		println('%s not present, not executing', name)
		return
	end
	f:close()

	local f = assert(exec_lookup_cfg(name)) -- unknown what happens if this fails
	f:close()

	local f = assert(exec_lookup_cfg(name)) -- unknown what happens if this fails
	local s = assert(f:read('a')) -- unknown what happens if this fails
	f:close()

	return cfg_eval_script(s)
end

--------------------------------------------------------------------------------

local cmd = setmetatable({}, {
	__index = function (self, k)
		local v = function (...)
			return cfg_eval_command({k, ...})
		end
		rawset(self, k, v)
		return v
	end,
})

for line in io.lines() do
	cfg_eval_script(line)
end
