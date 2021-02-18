# *cfgfs - scriptable configs*

<!-- marketing speech -->

For Source games - `cfgfs` allows writing configs in the
[Lua scripting language].

Keybinds and aliases can be defined, with full access to game commands,
variables and external Lua libraries.

Some of the other features available for `cfgfs` configs are:

- Reading messages written to the game console
- Waiting independently of the `sv_allow_wait_command` setting

No modifications to game files are needed: `cfgfs` works by creating a
[virtual filesystem] to dynamically generate config files with contents derived
from executing the Lua script.

[Lua scripting language]: https://www.lua.org/
[virtual filesystem]: https://en.wikipedia.org/wiki/Filesystem_in_Userspace

```lua
-- add a keybind
bind('f5', function ()
	cmd.say('Why did the chicken cross the road?')
	wait(2000)
	cmd.say('example test test 123 abc')
end)

-- define an alias (can be called from the game console)
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
- xterm (for `cfgfs_run`)

At the moment only GNU/Linux is supported. I don't have a Windows computer to
test with and I don't <del>want one</del> know if this will even work on it.

## Installation

1. Run `make && make install` to compile cfgfs and install the `cfgfs_run`
   script
2. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, add only `cfgfs_run`.

The cfgfs "config" will be loaded from `script_APPID.lua` where `APPID` is the
Steam App ID of the game.

(the scripts included with cfgfs are my configs, you'll probably want to erase
most of them)

## What else?

If you have any feedback/thoughts/questions/ideas/suggestions, I'd like to hear
about them in the [discussions tab]. Feel free to [open a new thread] and spill
your guts there.

[discussions tab]: https://github.com/huglovefan/cfgfs/discussions
[open a new thread]: https://github.com/huglovefan/cfgfs/discussions/new

If you can't get cfgfs to work (or have some other problem), then please
[open an issue]. Include at least a description of *what's wrong*, what game you
were trying to play and what GNU/Linux distro you're using.

[open an issue]: https://github.com/huglovefan/cfgfs/issues/new
