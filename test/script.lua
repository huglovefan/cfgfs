--
-- test that things still work
--



assert(cmd_stringify('test') == 'test')
assert(cmd_stringify('test', 'test') == 'test test')

-- space saving tricks
assert(cmd_stringify('aa', 'b b') == 'aa"b b')
assert(cmd_stringify('aa', 'b b', 'cc') == 'aa"b b"cc')
assert(cmd_stringify('aa', 'b b', 'c c') == 'aa"b b""c c')
assert(cmd_stringify('a a') == '"a a')
assert(cmd_stringify('') == '"')

-- say
assert(cmd_stringify('say', 'hi') == 'say"hi"')
assert(cmd_stringify('say', '"hi"') == 'say""hi""')

assert(cmd_stringify('say', 'aa', 'b"b', 'cc') == 'say"aa b"b cc"')

-- length limit
assert(nil ~= cmd_stringify(string.rep('a', 510)))
assert(nil == cmd_stringify(string.rep('a', 511)))

assert(nil ~= cmd_stringify('test', string.rep('a', 510-5)))
assert(nil == cmd_stringify('test', string.rep('a', 511-5)))

assert(nil ~= cmd_stringify('a  a', string.rep('a', 510-6)))
assert(nil == cmd_stringify('a  a', string.rep('a', 511-6)))



cmd.testtest = function ()
	cfg('123123')
end

cfg('init.cfg')



assert(type(fatal) == 'function')
add_listener('control', function (line)
	if line == 'ping1' then
		cfg('pong1')
	elseif line == 'ping2' then
		cfg('pong2')
	else
		fatal('unknown control line: ' .. line)
	end
end)



-- shut up the warning
bind('f11', 'cfgfs_click')
