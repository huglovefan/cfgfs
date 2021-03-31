# *cfgfs - scriptable configs*

<!-- marketing speech -->

For Source games - `cfgfs` allows writing configs in the
[Lua scripting language].

Keybinds and aliases can be defined, with full access to game commands,
variables and external Lua libraries.

Some of the other features available for `cfgfs` configs are:

- Reading messages written to the game console
- Waiting independently of the `sv_allow_wait_command` setting

No modifications to game files are needed: `cfgfs` is implemented using a
[virtual filesystem]. <sup>[\(tell me more\)]</sup>

[Lua scripting language]: https://www.lua.org/
[virtual filesystem]: https://en.wikipedia.org/wiki/Filesystem_in_Userspace
[\(tell me more\)]: #how-does-it-work

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

## How does it work?

As you might already know, binds and aliases in Source games can be made to
execute config files from the disk.

The epic trick is to combine this with a [virtual filesystem]. What this does is
let cfgfs handle all filesystem operations like file reads in the
*mount directory* <sup>[\(explain: mount point\)]</sup> that the virtual
filesystem is set up in.

By using `<game>/custom/!cfgfs/cfg/cfgfs` <sup>(`cfg/cfgfs` inside the
*custom folder* `!cfgfs`)</sup> as the mount directory, cfgfs will be able to
detect when the game tries to execute any config beginning with `cfgfs/`.

When the cfgfs filesystem gets a *read request* for a config file like
`cfgfs/aliases/greet.cfg`, it knows based on the path that it should look up the
function assigned to the `greet` alias and call it. Any console commands called
by the function are then returned to the game as the contents of the `greet.cfg`
file.

Aliases defined in the *cfgfs script* are automatically converted to ones like
`alias greet "exec cfgfs/aliases/greet.cfg"` and sent to the game the next time
it reads a config file from cfgfs.

[virtual filesystem]: https://en.wikipedia.org/wiki/Filesystem_in_Userspace
[\(explain: mount point\)]: http://www.linfo.org/mount_point.html

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
Steam App ID of the game. If it doesn't exist, then an empty one will be
automatically created.

## What else?

If you have any feedback/thoughts/questions/ideas/suggestions, I'd like to hear
about them in the [discussions tab]. Feel free to [open a new thread] and spill
your guts there.

If you can't get cfgfs to work (or have some other problem), then please
[open an issue]. Include at least a description of *what's wrong*, what game you
were trying to play and what GNU/Linux distro you're using.

[discussions tab]: https://github.com/huglovefan/cfgfs/discussions
[open a new thread]: https://github.com/huglovefan/cfgfs/discussions/new
[open an issue]: https://github.com/huglovefan/cfgfs/issues/new
