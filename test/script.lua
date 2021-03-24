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


add_listener('ping.1', function (data)
	if data then
		assert(data == 'ping1\n')
	else
		cfg('pong1')
	end
end)
add_listener('ping.2', function (data)
	if data then
		assert(data == 'ping2\n')
	else
		cfg('pong2')
	end
end)


-- shut up the warning
bind('f11', 'cfgfs_click')
