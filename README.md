# *cfgfs - scriptable configs*

<!-- marketing speech -->

`cfgfs` is a utility for Source games that extends the game's client-side
scripting capabilities.

It effectively augments the config system so that you can use [Lua] instead of
the clunky config language.

Aside from everything Lua and regular configs can do, `cfgfs` scripts can also
do things like

- Getting values of cvars (in addition to setting them)
- Reacting to messages written to the game console (chat messages, kill
  notifications)
- Running commands and retrieving their console output
- Waiting regardless of the `sv_allow_wait_command` setting

No modifications to game files are needed: `cfgfs` is implemented using a
[virtual filesystem].

[Lua]: https://www.lua.org/
[virtual filesystem]: https://en.wikipedia.org/wiki/Filesystem_in_Userspace

```lua
-- add a keybind
bind('f5', function ()
	cmd.say('Why did the chicken cross the road?')
	wait(1000)
	cmd.say('example test test 123 abc')
end)

-- define an alias (can be called from the in-game console)
cmd.greet = function ()
	local name = cvar.name
	cmd.echo('Hello ' .. name .. '!')
end
```

## Supported games

*This list is incomplete; you can help by [expanding it].*

- [Team Fortress 2](https://arch-img.b4k.co/vg/1607779368100.png)
- Fistful of Frags

[expanding it]: https://github.com/huglovefan/cfgfs/edit/master/README.md

## System requirements

- Lua 5.4
- `libfuse3`
- `libreadline`
- `libx11`, `libxtst`
- `libbsd`
- xterm (for `cfgfs_run`)
- standard development tools (`clang` or `gcc`, `git`, `make`)

#### Gentoo

```
sudo emerge -an lua:5.4 fuse:3 readline libX11 libXtst libbsd xterm git
```

#### Ubuntu and derivatives

At least Ubuntu 20.10 or newer is required due to older versions not having
the `liblua5.4-dev` package.

```sh
sudo apt install build-essential liblua5.4-dev libfuse3-dev libreadline-dev libx11-dev libxtst-dev libbsd-dev xterm
```

#### Windows

cfgfs works on GNU/Linux only. Just about everything in this repository is done
differently on Windows so most of the code would have to be rewritten for it.

Most importantly, the virtual filesystem library [`libfuse`] doesn't support
Windows. There exist Windows-native alternatives like [WinFsp] that could work
for making a cfgfs clone, but the work of figuring that out and writing the code
will have to be done by
[someone who's actually interested in using this thing on Windows].

[`libfuse`]: https://github.com/libfuse/libfuse
[WinFsp]: https://github.com/billziss-gh/winfsp
[someone who's actually interested in using this thing on Windows]: https://github.com/you

## Installation

1. Clone the repository and `cd` to it:
   `git clone https://github.com/huglovefan/cfgfs && cd cfgfs`
2. Run `make && make install` to compile cfgfs and install the `cfgfs_run`
   script
3. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, then add only
   `cfgfs_run`.

The cfgfs "config" will be loaded from `script_APPID.lua` in the `cfgfs`
directory where `APPID` is the Steam App ID of the game. If the file doesn't
exist, then an empty one will be automatically created.

Note that the `cfgfs_run` script will stop working if the `cfgfs` directory is
moved or renamed. In that event, the script will need to be reinstalled by
running `make install` again in the new directory.

#### Updating

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
