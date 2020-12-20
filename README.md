# *cfgfs - scriptable configs*

`cfgfs` lets you write binds and aliases for Source games in straight-forward
Lua code.

The "config" can use console commands, variables as well as external Lua
libraries.

`cfgfs` works by creating a
[virtual filesystem](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) to
generate configs "on the fly" as the game reads them. Keys are then bound
in-game to execute configs from there, which will let `cfgfs` react and call
your Lua function in response.

Using Lua features like metatables and coroutines underneath, `cfgfs` tries to
make the syntax for using game commands and variables as simple and natural as
possible.

```lua
-- bind a key
bind('f5', function ()
	cmd.say('Why did the chicken cross the road?')
	wait(2000)
	cmd.say('example test test 123 abc')
end)

-- define an alias
cmd.greet = function ()
	local name = cvar.name
	cmd.echo('Hello ' .. name .. '!')
end
```

## Supported games

- [Team Fortress 2](https://arch-img.b4k.co/vg/1607779368100.png)
- Fistful of Frags
- *probably others if you test them*

## System requirements

- Lua 5.4
- `libfuse3`
- `libreadline`
- `libx11`, `libxtst`

At the moment only GNU/Linux is supported. I don't have a Windows computer to
test with and I don't <del>want one</del> know if this will even work on it.

## Usage

it's not really ready yet

```sh
make start # start for TF2
make startfof # start for FoF
```
