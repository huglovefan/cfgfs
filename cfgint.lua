local cfgparser = {}

cfgparser.new = function ()
	local self = {}
	setmetatable(self, {__index = cfgparser})
	self.i = 1
	return self
end

cfgparser.skip_pattern = function (self, pattern)
	local startpos, endpos = self.source:find(pattern, self.i)
	if not startpos or startpos == endpos then
		return false
	end
	self.i = self.i + len
	return true
end

cfgparser.skip_comment = function (self)
	return self:skip_pattern('^//[^\n]*')
end

cfgparser.skip_whitespace = function (self)
	return self:skip_pattern('^[%z-% ]+')
end

local cfgint = {}

cfgint.new = function ()
	local self = {}
	setmetatable(self, {__index = cfgint})
	return self
end

local SEMICOLON = string.byte(';')

--[[
--|| parses one command from the input string
--]]
cfgint.parse_command = function (self, text, i)
	local r = {}
	if i > #text then return nil end
	if text:byte(i) <= 32 or text:byte(i) == SEMICOLON then
		return self:parse_command(text, i + 1)
	end
	while true do
		local len = text:find('^[%z-% ]+', i)
		if len ~= nil then i = i + len; goto next; end
		local len = text:find('^//[^\n]*', i)
		if len ~= nil then i = i + len; goto next; end
		
		::next::
	end
	return r, i
end
