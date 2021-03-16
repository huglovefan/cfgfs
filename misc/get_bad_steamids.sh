do_list() {
	echo "$1"
	mkdir -p misc/lists
	outfile=misc/lists/$1.json
	time_cond=
	if [ -e $outfile ]; then
		time_cond="--time-cond $outfile"
	fi
	curl \
	    --compressed \
	    --no-progress-meter \
	    --output "$outfile" \
	    --remote-time \
	    --tcp-fastopen \
	    ${time_cond} \
	    "$2"
	jq -r '.players[] | select(.attributes | index("cheater")) | .steamid' <"$outfile"
	if [ $? -ne 0 ]; then
		>&2 echo "error parsing list '$1'"
	fi
}
{
if [ -e misc/bots.list ]; then
	echo bots.list
	grep -Po '^\s*\K\[U:1:[0-9]+\]' misc/bots.list
fi
{
	echo cfgfs_log
	cat logs/*.log | grep -Po '(blu|red): .* \K\[U:1:[0-9]+\](?=: (impossible name|name stealer|known bot name))' | sort -Vu
}
# https://github.com/PazerOP/tf2_bot_detector/wiki/Customization#third-party-player-lists-and-rules
do_list milenko 'https://incontestableness.github.io/milenko-lists/playerlist.milenko-cumulative.json'
do_list pazer 'https://raw.githubusercontent.com/PazerOP/tf2_bot_detector/master/staging/cfg/playerlist.official.json'
do_list tf2bd 'https://tf2bdd.pazer.us/v1/steamids'
do_list wgetjane 'https://gist.githubusercontent.com/wgetJane/0bc01bd46d7695362253c5a2fa49f2e9/raw/playerlist.biglist.json'
} | awk '
	!/^\[/ { list=$0; next; }
	{
		if (steamids[$0] != "")
			steamids[$0] = steamids[$0] "," list;
		else
			steamids[$0] = list;
	}
	END {
		for (steamid in steamids)
			printf("%s\t%s\n", steamid, steamids[steamid]);
	}
' | sort -V
