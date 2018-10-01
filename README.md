# cfgfs

ever wanted to write configs in a worse language? now you can

## example

see `binds.js`

## usage

1. edit `binds.js`, add any missing keys in `keys.js`
2. ```bash
   custom_dir="/path/to/steamapps/common/Team Fortress 2/tf/custom/00-cfgfs"
   mkdir "$custom_dir"
   node main.js "$custom_dir"
   ```
