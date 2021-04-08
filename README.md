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
- standard development tools (`clang` or `gcc`, `git`, `make`)

### Gentoo

```
sudo emerge -an lua:5.4 fuse:3 readline libX11 libXtst xterm git
```

### Ubuntu and derivatives

At least Ubuntu 20.10 or newer is required due to older versions not having
the `liblua5.4-dev` package.

```sh
sudo apt install build-essential liblua5.4-dev libfuse3-dev libreadline-dev libx11-dev libxtst-dev xterm
```

### Windows

cfgfs works on GNU/Linux only. Just about everything in this repository is done
differently on Windows so most of the code would have to be rewritten for it.

Most importantly, the virtual filesystem library [`libfuse`] doesn't support
Windows. There exist Windows-native alternatives like [WinFsp] that could work
for making a cfgfs clone, but the work of figuring that out and writing the code
will have to be done by [someone who's actually interested in using Windows].

[`libfuse`]: https://github.com/libfuse/libfuse
[WinFsp]: https://github.com/billziss-gh/winfsp
[someone who's actually interested in using Windows]: https://github.com/you

## Installation

1. Clone the repository and `cd` to it:
   `git clone https://github.com/huglovefan/cfgfs && cd cfgfs`
2. Run `make && make install` to compile cfgfs and install the `cfgfs_run`
   script
3. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, add only `cfgfs_run`.

The cfgfs "config" will be loaded from `script_APPID.lua` in the `cfgfs`
directory, with `APPID` replaced by the game's App ID. If the file doesn't
exist, then an empty one will be automatically created.

Note that the `cfgfs_run` script will stop working if the `cfgfs` directory is
moved or renamed. In that event, the script will need to be reinstalled using
`make install` in the new directory.

### Updating

(in the `cfgfs` directory)

1. Pull the latest changes: `git pull`
2. Clean up the old version and recompile cfgfs: `make clean && make`

If any of the steps fail, please [open an issue].

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
