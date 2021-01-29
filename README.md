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
- xterm (for `cfgfs_run`)

At the moment only GNU/Linux is supported. I don't have a Windows computer to
test with and I don't <del>want one</del> know if this will even work on it.

## Installation

1. Run `make && make install` to compile cfgfs and install the `cfgfs_run`
   script
2. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, add only `cfgfs_run`.

The cfgfs "config" will be loaded from `script_APPID.lua` where `APPID` is the
Steam app id of the game.

(the ones included contain my configs, you'll probably want to delete most of
them)

## What else?

If you're using this, I'd like to hear any feedback and thoughts you might
have in the [discussions](https://github.com/huglovefan/cfgfs/discussions).
Open a new thread and describe your experience there. Any kind of comments are
appreciated, but I'm especially interested in hearing if there was anything you
found [*confusing*](https://en.wikipedia.org/wiki/Horror_and_terror),
[*excellent*](https://en.wikipedia.org/wiki/Ecstasy_(emotion)) or
[*missing*](https://en.wikipedia.org/wiki/Disappointment).

If you can't get it to work (or have some other problem), then please
[open an issue](https://github.com/huglovefan/cfgfs/issues/new). Include at
least a description of *what's wrong*, what game you were trying to play and
what GNU/Linux distro you're using.
