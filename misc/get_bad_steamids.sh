do_console_logs() {
	echo console.log
	cat \
	    ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf/console.log \
	    console.log \
	2>/dev/null | perl -0777 -e '
	foreach (<>=~m/# +[0-9]+ "(?=.{0,31}(?:valve|nigger|\n)).{0,32}"(?=[^\n]{40}) +(\[U:1:[0-9]+\]) +[:0-9]+ +[0-9]+ +[0-9]+ [a-z]+\n/gis) {
		print("$_\n");
	}
	' | awk '!t[$0]++'
}
do_list() {
	echo "$1"
	curl -s --compressed "$2" | jq -r '.players[] | select(.attributes | index("cheater")) | .steamid'
	if [ $? -ne 0 ]; then
		>&2 echo "error parsing list '$1'"
	fi
}
{
do_console_logs
# https://github.com/PazerOP/tf2_bot_detector/wiki/Customization#third-party-player-lists-and-rules
do_list milenko 'https://incontestableness.github.io/milenko-lists/playerlist.milenko-cumulative.json'
do_list pazer 'https://raw.githubusercontent.com/PazerOP/tf2_bot_detector/master/staging/cfg/playerlist.official.json'
do_list tf2bd 'https://tf2bdd.pazer.us/v1/steamids'
do_list wgetjane 'https://gist.githubusercontent.com/wgetJane/0bc01bd46d7695362253c5a2fa49f2e9/raw/playerlist.biglist.json'
} | awk '
	!/^\[/ { list=$0; next; }
	{
		if (steamids[$0] != "")
			steamids[$0] += "," list;
		else
			steamids[$0] = list;
	}
	END {
		for (steamid in steamids)
			printf("%s\t%s\n", steamid, steamids[steamid]);
	}
' | sort -V
