# *cfgfs - scriptable configs*

`cfgfs` is a utility for Source games that extends the game's client-side scripting capabilities.

It makes it so that configs can be written in the [Lua scripting language].

Aside from everything Lua and regular configs can do, cfgfs scripts can also do things like:

- Getting values of cvars (not only setting them)
- Reacting to messages written to the game console (chat messages, kill notifications)
- Running commands and retrieving their console output
- Waiting regardless of the `sv_allow_wait_command` setting

All of this is done safely without modifying the game process or its binaries.
To the game, it's the same as if it was executing regular config files from the disk.

```Lua
-- add a keybind
bind('f10', function ()
	cmd.say('Why did the chicken cross the road?')
	wait(1000)
	cmd.say('example test test 123 abc')
end)

-- define an alias (can be called from the in-game console)
cmd.greet = function ()
	local myname = cvar.name
	cmd.echo('Hello ' .. myname .. '!')
end
```

[Lua scripting language]: https://www.lua.org/about.html

## System requirements

- Lua 5.4
- `libfuse3`
- `libreadline`
- `libx11`, `libxtst`
- `libbsd`
- xterm
- standard development tools (`clang` or `gcc`, `git`, `make`)

#### Gentoo

```sh
sudo emerge -an lua:5.4 fuse:3 readline libX11 libXtst libbsd xterm git
```

#### Ubuntu

Requires at least Ubuntu 20.10 or newer for the `liblua5.4` package.

```sh
sudo apt install build-essential liblua5.4-dev libfuse3-dev libreadline-dev libx11-dev libxtst-dev libbsd-dev xterm
```

#### Windows

`cfgfs` currently doesn't support Windows.
Just about everything in this repository is done differently on Windows so most of the code would have to be rewritten for it.

Most importantly, the virtual filesystem library [`libfuse`] doesn't support Windows.
There exist Windows-native alternatives like [WinFsp] that could work for making a cfgfs clone.

[`libfuse`]: https://github.com/libfuse/libfuse
[WinFsp]: https://github.com/billziss-gh/winfsp

## Installation

1. Clone the repository and `cd` to it: `git clone https://github.com/huglovefan/cfgfs && cd cfgfs`
2. Run `make && make install` to compile cfgfs and install the `cfgfs_run` script
3. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, then add only `cfgfs_run` before `%command%`.

Launching the game will now automatically start `cfgfs` and load the "config" from `script_APPID.lua` (with the game's AppID) in the cfgfs directory.

**Note:** If the cfgfs directory is ever moved or renamed, the `cfgfs_run` script will need to be reinstalled by running `make install` again in the new directory.

#### Updating

(in the `cfgfs` directory)

1. Pull the latest changes: `git pull`
2. Clean up the old version and recompile cfgfs: `make clean && make`

If any of the steps fail, please [open an issue].

[open an issue]: https://github.com/huglovefan/cfgfs/issues/new
