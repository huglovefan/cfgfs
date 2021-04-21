#
# this script is sourced before cfgfs is started
# use it to export environment variables or something
#

# WARNING: copy this file out of the examples/ directory first if you're going
#  to use it. it must be next to the "cfgfs_failsafe.sh" file to work

# if your cfgfs scripts are in a different directory, "cd" to it here
#cd ~/src/cfgfs-scripts/ || exit

# if you have rcon enabled, set these to be able to use rcon() from lua
# launch options to enable rcon: -usercon +rcon_password changeme +net_start
#export CFGFS_RCON_HOST=localhost
#export CFGFS_RCON_PORT=27015
#export CFGFS_RCON_PASSWORD="changeme"
