<!-- marketing speech -->

# *cfgfs - scriptable configs*

`cfgfs` is a
[virtual filesystem](https://en.wikipedia.org/wiki/Filesystem_in_Userspace)
providing more expressive configs for Source gamers.

Instead of the limiting `cfg` syntax, binds and aliases can be written in fully
Turing-complete Lua 5.4.

`cfgfs` works by creating a virtual filesystem in the `cfg` directory. When a
keybind is added in `script.lua`, that key is bound in-game to execute a config
corresponding to it on keypress and release. This will let `cfgfs` know when
it's pressed/released and allows us to return commands to be run in response to
those events.

Some other interesting features of `cfgfs` are:

- Timers and timeouts for executing commands not just in response to keypresses
  (works by simulating a keypress after a delay)
- Reading output from the game console (using the log file)
  - Can get values of variables
  - Can run console commands and read their output
- Detecting when other configs are executed
  - Can detect class/map changes in TF2
  - Can prevent other configs from running (like the mastercomfig ad banner)
- ???

## Supported games

- [Team Fortress 2](https://www.etlegacy.com/)

- [Fistful of Frags](https://store.steampowered.com/app/440/Team_Fortress_2/)
  - Supported using a small
    [compatibility workaround](https://github.com/huglovefan/cfgfs/blob/154325a/builtin.lua#L298-L307)
    due to the game having
    [removed](https://steamcommunity.com/games/fof/announcements/detail/199616928162078893)
    the `alias` command.  
    This affects the ability to do keybinds accurately.  
    `cfgfs` still receives key events, but has to guess whether the key was just
    pressed down or released based on its previous assumed state.  
    If the state becomes wrong due to missed press/release events, this could
    potentially lead to keys getting "stuck" (but there were no problems during
    my limited testing.)

## System requirements

- Lua 5.4
- `libfuse3`
- `libreadline`
- `libx11`, `libxtst`
- `libjemalloc`
- `clang` compiler

Due to lack of interest and development resources, `cfgfs` is currently not
compatible with the Microsoft® Windows® operating system.

<sup>Microsoft, Windows and Microsoft Windows are either registered trademarks,
trademarks, or products of Microsoft Corporation in the United States and/or
other countries.</sup>

<sup>Product names used herein are for identification purposes only and might be
trademarks of their respective companies. We disclaim any and all rights to
those marks.</sup>

## Usage

it's not really ready yet

especially everything in `builtin.lua` is subject to change until i've organized
it, found the best ways to do everything and documented it

```sh
make
make start # start for TF2
make startfof # start for FoF
```
