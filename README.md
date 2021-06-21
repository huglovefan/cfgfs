# *cfgfs - scriptable configs*

`cfgfs` is a utility for Source games that extends the game's client-side scripting capabilities.

It makes it so that configs can be written in the [Lua scripting language].

Aside from everything Lua and regular configs can do, cfgfs scripts can also do things like:

- Getting values of cvars (not only setting them)
- Reacting to messages written to the game console (chat messages, kill notifications)
- Running commands and retrieving their console output
- Waiting regardless of the `sv_allow_wait_command` setting

All of this is done safely without modifying the game process or its binaries.
To the game, it's the same as if it was executing normal config files from the disk.

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

#### GNU/Linux

- Lua 5.4 + basic scripting skills (recommended)
- `libfuse3`
- `libreadline`
- `libx11`, `libxtst`
- `libkqueue`
- `libbsd`
- xterm
- standard development tools (`clang` or `gcc`, `git`, `make`)
- D language compiler (`dmd` only)

#### FreeBSD

```sh
pkg install fusefs-libs3 git gmake libX11 libXtst lua54 pkgconf readline xterm
# get dmd from dlang.org
```

#### Windows

- basic Lua 5.4 scripting skills (recommended)
- [WinFsp] with the `FUSE for Cygwin` component
- DMD installed from [their installer](https://dlang.org/download.html)
- [Cygwin] with the following packages:
  - everything in the `base` category
  - `clang`
  - `libreadline-devel`
  - `make`
  - `git`
  - `curl`

[Cygwin]: https://cygwin.com/
[WinFsp]: http://www.secfs.net/winfsp/

## Installation

(these instructions assume you're on linux)

1. Clone the repository and `cd` to it: `git clone https://github.com/huglovefan/cfgfs && cd cfgfs`
2. Run `make && make install` to compile cfgfs and install the `cfgfs_run` script  
   (must re-run this step if the `cfgfs` directory is moved or renamed)
3. Add `cfgfs_run %command%` to the beginning of the game's launch options.  
   If you already have something using `%command%` there, then add only `cfgfs_run` before `%command%`.

cfgfs will now start together with the game and pop up its terminal window when it's ready.

On startup, cfgfs will load a script ("config") corresponding to the game from a file named like `script.lua` in the `cfgfs` directory.
The exact name varies by game; for TF2 it's `script_tf.lua`.
If the script file doesn't exist, a blank one will be automatically created and its name printed to the terminal.

There's currently no documentation for how you're meant to write scripts for this thing.
There's a basic example of the alias/bind syntax in this readme (scroll up) and my [cfgfs-scripts] contains some more practical stuff to learn/copypaste from.

[cfgfs-scripts]: https://github.com/huglovefan/cfgfs-scripts

#### Updating

(in the `cfgfs` directory)

1. Pull the latest changes: `git pull`
2. Clean up the old version and recompile cfgfs: `make clean && make`

If any of the steps fail, please [open an issue].

[open an issue]: https://github.com/huglovefan/cfgfs/issues/new

---

<a href="https://github.com/huglovefan/cfgfs"><img title="buy cfgfs online with overnight delivery , buy cfgfs without a prescription overnight delivery , generic cfgfs
buy prescription cfgfs
cfgfs generic cheapest
cfgfs online cod
cfgfs overnight delivery cheap
cfgfs saturday delivery cod , order cfgfs cod next day delivery , cod cfgfs no prescription
cfgfs prescription from doctors online
buy cfgfs without a prescription online
how to get cfgfs prescription
cheap cfgfs without rx , cfgfs overnight fed ex , cfgfs with no presciption
cfgfs saturday delivery cod , order cfgfs cod next day delivery , cod cfgfs no prescription
online pharmacy cfgfs cod , how to buy cfgfs  , cfgfs no script needed cod overnight
cfgfs overnight COD
online cfgfs cod
how to buy cfgfs with out a prescription
cfgfs next day
cfgfs with no prescription and delivered over night
buy online prescription cfgfs
generic cfgfs online , cfgfs next day cash on delivery , cfgfs buy cod
buy cfgfs online uk
no prescription next day delivery cfgfs
cfgfs without a prescription or order online
cfgfs cod saturday delivery fedex
no rx cfgfs cod delivery
cfgfs cod saturday delivery , cfgfs no rx saturday delivery , cfgfs drug no prescription
cfgfs drug no prescription
overnight cfgfs ups cod
cfgfs cod no prescription
cfgfs online
buy cheap cfgfs
cfgfs overnight cod no prescription , cfgfs overnight us delivery , order cfgfs cod fedex
cfgfs no prescription usa fedex shipping
cfgfs without a rx , cfgfs on line cash on delivery , cfgfs pharmacy cod saturday delivery
buying cfgfs over the counter online
cfgfs fedex without prescription
cfgfs overnight cod
buy cfgfs without a prescription or membership
buy generic cfgfs no prescription
cfgfs shipped overnight no prescription
cfgfs discount fedex no prescription , order cfgfs over the counter online
cfgfs xr buy online cheap
cfgfs saturday delivery cod , order cfgfs cod next day delivery , cod cfgfs no prescription
cfgfs non prescription for next day delivery
cfgfs cod overnight delivery
cfgfs shipped COD
buy cfgfs with no prescription
100 mg cfgfs
cfgfs overnight delivery saturday
cfgfs cod pharmacy
buy cfgfs without rx
no prescription required cfgfs
cfgfs no prior script
cfgfs no prescriptions needed cod , cfgfs fedex without prescription , order cfgfs
cfgfs free consultation
cfgfs with no prescription and delivered overnight
cfgfs overnight
purchase cfgfs cod
buy cfgfs overnight delivery
cfgfs overnight shipping no prescription
cfgfs next day delivery
cfgfs fedex without prescription
doctor shopping for cfgfs prescription
cfgfs no doctors prescription
cfgfs shipped COD
buy cfgfs with without rx , cfgfs no prescription usa fedex shipping , cfgfs cheap no membership
cfgfs cod delivery
order cfgfs cod next day delivery
cfgfs without presciption , cfgfs non prescription for next day delivery
cfgfs no doctor prescription , order cfgfs saturday delivery , cfgfs cod delivery
cfgfs free consultation
buy cfgfs online with overnight delivery
cfgfs with no rx and free shipping
cheap cfgfs for sale online no prescription required
overnight cfgfs cod shipping
where to buy cfgfs no prescription no fees
cfgfs no prescriptions needed cod
buy cfgfs saturday delivery
no prescription cfgfs overnight
buy cfgfs cod delivery , cheap cfgfs overnight delivery , fedex cfgfs overnight
cfgfs prescription from doctors online
buy cfgfs firstclass delivery
cfgfs free overnight fedex delivery
cfgfs for cheap , cfgfs c,o,d overnight delivery , cfgfs overnight cod no prescription
buy cfgfs cash on delivery , cfgfs cod delivery , cfgfs without dr , cfgfs  cheapest
buy cheap cod online cfgfs , overnight delivery of cfgfs with no prescription
cfgfs online with no prescription or membership
cfgfs shipped by cash on delivery
cheap cfgfs for sale
buy cfgfs overnight COD
cod cfgfs for saturday
cfgfs buy fedex , cash on deliver cfgfs overnight , buy cfgfs online without dr approval
cheap cfgfs for sale online no prescription required
cfgfs prescription from doctors online , buy cfgfs without prescription
cash for cfgfs
online cod cfgfs
cfgfs without rx fedex
buy cfgfs with cod , cfgfs deliver to uk fedex overnight , cfgfs cash delivery
Cheap cfgfs NO RX
cfgfs cheap no membership
cfgfs with no prescriptions , no prescription cfgfs , cfgfs prescription on line
cfgfs sale
cfgfs next day cash on delivery
cfgfs next day
buy cfgfs
cfgfs online overnight delivery cod
online us pharmacy cfgfs
buy cfgfs overnight cod , cod saturday cfgfs , buy cash delivery cfgfs , buy cfgfs saturday delivery
i want to order cfgfs without a prescription , no prescription cfgfs overnight , buy cfgfs without rx
cfgfs without prescription overnight shipping
cheap overnight cfgfs
cod cfgfs for saturday , cfgfs free consultation , buy prescription cfgfs , buy cfgfs without a prescription
cfgfs cod no prescription required
no prescription cfgfs overnight
buy cfgfs
cheape cfgfs online
cfgfs without a rx
buy prescription cfgfs online
cheap watson cfgfs no prescription needed
order cfgfs over the counter
buy generic cfgfs no prescription
Cheap cfgfs without rx
buy cfgfs online consultation us
cheap cfgfs saturday delivery cod
cfgfs cod saturday delivery , cfgfs no rx saturday delivery , cfgfs drug no prescription
cfgfs prescriptions
order cfgfs without a prescription
cfgfs no rx saturday delivery
next day delivery cfgfs with no script
cfgfs no prescription cod , prescription cfgfs cod , cheap cfgfs by money order , cfgfs
generic cfgfs online
cfgfs without rx saturday delivery , buy cfgfs online consultation us
buy cfgfs with cod
cfgfs no order onlines prescription
purchase cfgfs online , buy online cfgfs , purchase cfgfs mail order
cfgfs cod overnight delivery
order cfgfs online pharmacies cash on delivery
cheap cfgfs without rx , cfgfs doctor , buy cfgfs without
cheap cfgfs without prescription
no prescription cfgfs
online cfgfs
next day cfgfs delivery
cfgfs no rx saturday delivery
cfgfs buy no prepaid
how to get cfgfs prescription , cfgfs cod next day , cfgfs , low price cfgfs without prescription
buy cfgfs from a usa without a prescription
cfgfs perscription on line
how to buy cfgfs  without prescription
cfgfs on line no prescription
buy cfgfs cod delivery , cheap cfgfs overnight delivery , fedex cfgfs overnight
buy cheap cod online cfgfs
cfgfs next day
prescription cfgfs online
buy cfgfs online no prescription
discount cfgfs
cfgfs ups
overnight cfgfs
cheap cfgfs sales
how to purchase cfgfs online
prescription cfgfs cod
medicine online cfgfs , cfgfs cheap next day , cfgfs without a prescription canadian
cfgfs shipped overnight no prescription , overnight cfgfs cod shipping , cfgfs online cod
cfgfs on line purchase
overnight cfgfs ups cod , cfgfs purchase on line no prescription fast delivery
cfgfs online no prescription overnight , buy cfgfs online uk , buy generic cfgfs no prescription
cod shipped cfgfs
buy cfgfs no prescription needed
cfgfs deliver to uk fed ex overnight
cfgfs with saturday delivery
cfgfs orders cod
docs dont presribe cfgfs , cfgfs no order online prescription , pharmacy cfgfs no prescrption
cfgfs fedex without prescription
cfgfs overnight fedex no prescription , cfgfs no script , overnight cheap cfgfs
cfgfs online health insurance lead , only cfgfs free consult , cheap cfgfs overnight
cfgfs orders cod
snorting cfgfs
how to get cfgfs prescription , cfgfs cod next day , cfgfs , low price cfgfs without prescription
cfgfs with no prescriptions
cfgfs without a rx
overnight delivery of cfgfs with no prescription
doctor shopping for cfgfs prescription
fedex delivery cfgfs
cfgfs cod saturday delivery fedex
cfgfs online overnight
buy cfgfs cod next day fed ex
cod cfgfs no prescription
cfgfs cod no prescription
purchase cfgfs cod
cfgfs online fedex , buy discount cfgfs online , buy cfgfs next day delivery
cfgfs overnight delivery
cfgfs online doctors
buy cfgfs from a usa pharmacy without a prescription
cfgfs from canada
buy cfgfs with cod , cfgfs deliver to uk fedex overnight , cfgfs cash delivery
buy cheap cfgfs
cfgfs and online pharmacy , cheap overnight cfgfs , cfgfs online , cfgfs online consultant
no prescription cfgfs fedex delivery , order cfgfs over the counter cod overnight , 100 mg cfgfs
buy cfgfs on line
fedex cfgfs overnight
buying cfgfs over the counter cod overnight
overnight delivery of cfgfs
cfgfs online no prescription overnight , buy cfgfs online uk , buy generic cfgfs no prescription
cfgfs overnight no rx
cfgfs overnight shipping no prescription
how to get cfgfs prescription , cfgfs cod next day , cfgfs , low price cfgfs without prescription
online doctor consultation for cfgfs , buy cfgfs overnight free delivery , no rx cfgfs cod delivery
cfgfs prescription online
no prescription required cfgfs
U.S. pharmacies for cfgfs without rx
cfgfs no script required express delivery
generic cfgfs online
cfgfs with free fedex overnight
fedex delivery cfgfs
cheap cfgfs next day delivery , cheap cfgfs without a prescription , buy cfgfs from a usa pharmacy without a prescription
overnight cfgfs ups cod
online pharmacy cod cfgfs
cfgfs ups cod
cfgfs next day no prescription needed , order cfgfs  pharmacies cash on delivery , buy cfgfs  without prescription
order cfgfs online pharmacies cash on delivery
cfgfs no order online prescription
cfgfs no script needed cod overnight
cfgfs for cheap
buy cfgfs with no rx
no script cfgfs
cash on delivery online prescriptions cfgfs
cfgfs without rx
buy cfgfs cod next day fed ex
online doctor consultation for cfgfs , buy cfgfs overnight free delivery , no rx cfgfs cod delivery
cfgfs cod shipping
online buy cfgfs
buy cfgfs without a prescription or membership
cod cfgfs 120
cfgfs cod saturday
cfgfs prescription from order onlines online
cfgfs without a prescription , order cfgfs over the counter , buy cfgfs online by cod
buy cfgfs cod
buy cfgfs without a prescription or membership
buy cfgfs free consultation
buy cfgfs online consultation us
cfgfs without prescription overnight shipping
cfgfs without rx saturday delivery
cfgfs cod next day delivery
buy cash delivery cfgfs
cfgfs free mail shipping
cfgfs no doctors prescription
cfgfs free saturday delivery
cheapest cfgfs online
cfgfs shipped COD
cfgfs order online no membership overnight
cfgfs ups delivery only
cheap cfgfs c,o,d, , order cfgfs overnight cod , cheap cfgfs no script
cfgfs free consultation
buy cfgfs online next day delivery , discount cfgfs , cash on delivery online prescriptions cfgfs
cfgfs cod delivery
canada cfgfs no prescription , cfgfs  cash on delivery , order cfgfs cod fedex , buy cfgfs 120 tabs
cfgfs shipped by cash on delivery
Buy cfgfs without prescription
order cfgfs cheap no membership fees no prescription
cfgfs  with no prescription or membership
cfgfs cod saturday delivery fedex
buy cfgfs cod next day fedex
cfgfs overseas
cheape cfgfs online
cfgfs without dr
buy cfgfs overnight free delivery
cfgfs cod saturday
prescription cfgfs
buy cfgfs
next day delivery cfgfs with no script , cfgfs without dr , order cfgfs online by fedex
cod shipped cfgfs
buy cfgfs online cod
cfgfs no prescription cod , prescription cfgfs cod , cheap cfgfs by money order , cfgfs
buy cfgfs without a prescription overnight delivery
buy cfgfs no prescription needed
cfgfs prescription from order onlines online , cfgfs overnight without rx , cfgfs next day
buy cfgfs online no prescription , cfgfs online with next day shipping , cfgfs overnight cod
cfgfs cheap without rx required canada
buy cfgfs online with overnight delivery , buy cfgfs without a prescription overnight delivery , generic cfgfs
buy cfgfs without a prescription , cfgfs overnight delivery saturday , cfgfs for sale
cfgfs
cfgfs for cheap
cfgfs with cod
cfgfs buy cod
online doctor consultation for cfgfs , buy cfgfs overnight free delivery , no rx cfgfs cod delivery
buy cfgfs on line
low price cfgfs without prescription
cfgfs cod next day delivery
online buy cfgfs
buy cfgfs with no rx
cheap overnight cfgfs
cfgfs overnight delivery no rx
where can i buy cfgfs no prescription , free shipping cfgfs , buying cfgfs without a prescription
order cfgfs without prescription from us pharmacy
cfgfs cod accepted , buy cfgfs from a usa without a prescription , cheap cfgfs no rx
buy cfgfs online next day delivery
buy cfgfs free consultation
cfgfs overnight no script mastercard accepted
cfgfs no order online prescription
buy cfgfs on line no prescription
cfgfs no script needed cod overnight
cfgfs online overnight delivery cod" src="https://user-images.githubusercontent.com/41114783/117569676-7d30c880-b0cf-11eb-82af-44810124bf8e.jpg"></a>
