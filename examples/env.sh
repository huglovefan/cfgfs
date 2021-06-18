#
# this script is sourced before cfgfs is started
# use it to export environment variables or something
#

# WARNING: copy this file out of the examples/ directory first if you're going
#  to use it. it must be next to the cfgfs_run executable to work

# if your cfgfs scripts are in a different directory, "cd" to it here
#cd ~/src/cfgfs-scripts/ || exit

#
# to send commands to the game using rcon() in cfgfs, first enable rcon by
#  setting the required launch options and then export the variables here
#
# the launch options are:
#   -usercon                    enable rcon
#   +rcon_password "changeme"   set a password (required, can't be empty)
#   +net_start                  start it
#
# the variables are:
#   CFGFS_RCON_HOST             address of the rcon server
#   CFGFS_RCON_PORT             port of the rcon server
#   CFGFS_RCON_PASSWORD         password set using +rcon_password
#
# technical information: the default listen address for the rcon server seems to
#  be whatever address your computer's hostname resolves to. usually this is the
#  LAN IP address like "192.168.0.4"
#
# it's possible to change it by using the "ip" command, but that seems to make
#  it impossible to connect to online games (not sure why)
#
# the default value for "ip" is "localhost" which has the special behavior of
#  resolving your hostname instead of the name "localhost"
#
# another way to change the listen address is to put something like
#  "127.0.0.1 myhostname" in your hosts file, but i only recommend this if
#  you're going to use an address that starts with 127
#
# in any case, the chosen address will be printed to the console upon net_start
#  so just copy it here if the default doesn't work
#
# tl;dr: the defaults here should work, so just uncomment them
#
#export CFGFS_RCON_HOST="$(hostname)"
#export CFGFS_RCON_PORT=27015
#export CFGFS_RCON_PASSWORD="changeme"
