mnt="${HOME}/.local/share/Steam/steamapps/common/Team Fortress 2/tf/custom/00-cfgfs/cfg"
script="$(realpath ./main.lua)"
mkdir -p "$mnt"
fusermount -u "$mnt" 2>&-
CFGFS_SCRIPT="$script" exec ./cfgfs -f -o auto_unmount "$mnt"
