import std.file;
import std.stdio;
import std.string;
import std.conv;
import std.path;
import std.array;
import std.process;
import std.regex;
import std.parallelism;
import std.datetime.systime;
import std.concurrency;
import std.datetime.stopwatch;

import core.runtime;
import core.thread.osthread;
import core.time;
import core.stdc.stdlib;

import core.sys.windows.windows;

string findGameExe(string[] args) {
	foreach (arg; args) {
		if (arg.isAbsolute) {
			switch (arg.baseName) {
			case "hl2.sh":
			case "hl2.exe":
				return arg;
			default:
				break;
			}
		}
	}
	return null;
}

string findGameDir(string[] args, string exeDir) {
	// note: use the last one in case there are multiple
	// note2: the path is relative to the executable (hl2.sh or hl2.exe)
	string rv = null;
	for (int i = 1; i < args.length-1; i++) {
		if (args[i] == "-game") {
			rv = args[i+1].absolutePath(exeDir);
			i += 1;
		}
	}
	if (!rv) {
		throw new StringException("couldn't parse command line (missing -game parameter)");
	}
	return rv;
}

string findGameTitle(string gameDir) {
	auto gameinfo = File(gameDir.chainPath("gameinfo.txt"), "r");
	foreach (line; gameinfo.byLine) {
		if (auto m = line.matchAll("^[\0-\x20]*game[\0-\x20]+\"(.*)\"[\0-\x20]*$")) {
			return cast(string)m.front[1];
		}
	}
	throw new StringException("couldn't parse gameinfo.txt");
}

version (Windows) {

bool isCfgfsReady(string mountpoint) {
	auto path = mountpoint.chainPath("cfgfs", "buffer.cfg");
	try {
		auto f = File(path, "r");
		f.destroy();
		return true;
	} catch (Exception e) {
		return false;
	}
}

void runCfgfsCommand(string title, string[] cmd, string[string] env) {
	// note: mintty "forks" into a new process to display its gui so .wait() doesn't work
	// use -RP to make it print the child pid to stdout and wait that instead

	string[] minttyCmd = [
		"C:\\cygwin64\\bin\\mintty.exe",
		//"-h", "error",
		"-RP",
		"-t", title,
		"-e",
	];

	// do env.sh if it exists and cygwin is installed
	// (may want to support using this without cygwin at some point)
	if (exists(env["CFGFS_DIR"].chainPath("env.sh")) &&
	    exists("C:\\cygwin64\\bin\\cygpath.exe") &&
	    exists("C:\\cygwin64\\bin\\sh.exe")) {
		minttyCmd ~= [
			"/bin/sh", "-c", `
				[ ! -e env.sh ] || . ./env.sh
				if exe=$(/bin/cygpath.exe -- "$1"); then
					shift
					set -- "$exe" "$@"
				fi
				exec "$@"
			`, "--",
		];
	}

	auto pipe = pipe();
	auto p = spawnProcess(
		minttyCmd ~ cmd,
		std.stdio.stdin,
		pipe.writeEnd,
		std.stdio.stderr,
		env,
		Config.none,
		env["CFGFS_DIR"]
	);
	p.wait(); // useless

	// it's written like `printf("%d %d\n", pid1, pid2)`
	auto p1 = to!int(pipe.readEnd.readln(' ')[0..$-1]);
	auto p2 = to!int(pipe.readEnd.readln('\n')[0..$-1]);
	pipe.close();

	HANDLE pid1 = OpenProcess(SYNCHRONIZE, false, p1);
	HANDLE pid2 = OpenProcess(SYNCHRONIZE, false, p2);

	if (pid1 != null) {
		WaitForSingleObject(pid1, INFINITE);
		CloseHandle(pid1);
	}
	if (pid2 != null) {
		WaitForSingleObject(pid2, INFINITE);
		CloseHandle(pid2);
	}
}

bool checkMountBeforeRun(bool tryRecover) {
	if (isCfgfsReady(cfgfsMountPoint)) {
		MessageBoxA(null, "Looks like another instance of cfgfs is already running for this game. Kill the cfgfs.exe process and try again.".toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
		return false;
	}

	bool mountDirExists = cfgfsMountPoint.exists;

	// if the permissions on the mount directory/junction become
	//  sufficiently fugged up, .exists() will return false but
	//  .getAttributes() will throw an ERROR_ACCESS_DENIED
	// drwxr-x---  1 Unknown+User Unknown+Group 0 Jun  8 13:49 cfg/
	// edit: rebooting seems to fix this
	if (!mountDirExists) {
		try {
			cfgfsMountPoint.getAttributes();
		} catch (FileException e) {
			switch (e.errno) {
			case ERROR_FILE_NOT_FOUND: // doesn't exist (2)
				mountDirExists = false;
				break;
			case ERROR_ACCESS_DENIED: // permission denied (5)
				mountDirExists = true;
				break;
			default: // ?
				throw e;
			}
		}
	}

	if (mountDirExists) {
		// try to delete the mount directory if it already exists
		// if it's mounted, this will succeed but it'll keep existing
		// (but if the other check failed then it's probably not mounted)
		if (tryRecover) {
			bool deleted = false;
			try {
				cfgfsMountPoint.rmdir();
				deleted = true;
			} catch (Exception) {
			}
			if (deleted) return checkMountBeforeRun(false);
		}
		MessageBoxA(null, "The mount directory already exists. Make sure cfgfs isn't already running.".toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
		return false;
	}
	return true;
}

} // version (Windows)

version (Posix) {

bool isCfgfsReady(string mountpoint) {
	// test readiness by opening and closing a file from the filesystem
	// this uses timeout to avoid freezing if the filesystem is in a bad state (frozen or crashed but left mounted)
	auto p = spawnProcess(
		["timeout", "0.1", "sh", "-c", `exec 2>/dev/null; exec <"$1/cfgfs/buffer.cfg"`, "--", mountpoint],
		[
			"LD_LIBRARY_PATH": null,
			"LD_PRELOAD": null,
		],
		Config.retainStdin|Config.retainStdout|Config.retainStderr);
	return (0 == p.wait());
}

void linkConsoleLog(string gameDir, string mountPoint) {
	string log = cast(string)gameDir.chainPath("console.log").array;
	string target = cast(string)mountPoint.chainPath("console.log").array;
	if (log.exists) {
		log.rename("console.log.old");
	}
	symlink(target, log);
}
void unlinkConsoleLog(string gameDir, string mountPoint) {
	string log = cast(string)gameDir.chainPath("console.log").array;
	if (log.isSymlink) {
		log.remove();
	}
}

void runCfgfsCommand(string title, string[] cmd, string[string] env) {
	spawnProcess(
		[
			"xterm",
				"-title", title,
				"-e",
				"sh", "-c", `[ ! -e env.sh ] || . ./env.sh; exec "$@"`, "--",
		] ~ cmd,
		env,
		Config.retainStdin|Config.retainStdout|Config.retainStderr,
		env["CFGFS_DIR"]
	).wait();
}

version (linux)
bool checkMountBeforeRun(bool tryRecover) {
	// context: we're about to mount cfgfs, but it needs to be not already mounted before we can do that

	auto pipe = pipe();
	auto p = spawnProcess(
		["fusermount", "-u", cfgfsMountPoint],
		std.stdio.stdin,
		std.stdio.stdout,
		pipe.writeEnd,
		[
			"LANG": "C",
			"LD_LIBRARY_PATH": null,
			"LD_PRELOAD": null,
		]
	);
	pipe.writeEnd.close();
	p.wait();
	string msg = cast(string)pipe.readEnd.byLine().join("");
	pipe.readEnd.close();

	if (-1 != msg.indexOf("fusermount: entry for ") &&
	    -1 != msg.indexOf(" not found in /etc/mtab")) {
		// ok: was not mounted
		return true;
	}

	if (msg == "") {
		// ok: successfully unmounted it
		return true;
	}

	if (-1 != msg.indexOf("fusermount: failed to unmount ") &&
	    -1 != msg.indexOf(": Device or resource busy")) {
		// it's either
		// 1. still mounted, but a process has a file open so it can't be unmounted
		// 2. exited uncleanly (kill -9 or abort()), and a process had a file open

		if (isCfgfsReady(cfgfsMountPoint)) {
			// running
			return false;
		} else {
			// crashed
			// it seems that "fusermount -z" can unmount these without root privileges
			if (tryRecover) {
				spawnProcess(
					["fusermount", "-uz", cfgfsMountPoint],
					std.stdio.stdin,
					std.stdio.stdout,
					std.stdio.stderr,
					[
						"LD_LIBRARY_PATH": null,
						"LD_PRELOAD": null,
					]
				).wait();
				return checkMountBeforeRun(false);
			} else {
				return false;
			}
		}
	}

	// bad: unknown error
	// the old version ignored these so just return true
	version (Posix) stderr.writeln(msg);
	return true;
}

version (FreeBSD)
bool checkMountBeforeRun(bool tryRecover) {
	if (isCfgfsReady(cfgfsMountPoint)) {
		return false;
	}
	return true;
}

} // version (Posix)

string termTitleForGameName(string gameName) {
	return "cfgfs (%s)".format(gameName);
}

version (Windows) string exeName = "cfgfs.exe";
version (Posix) string exeName = "cfgfs";

// -----------------------------------------------------------------------------

shared string cfgfsDir;
shared string gameRoot;

shared string gameDir;
shared string gameTitle;

shared string cfgfsMountPoint;

shared bool gameExited;

void runMain(string[] args) {

	cfgfsDir = thisExePath().dirName;
	gameRoot = getcwd();

	string exePath = findGameExe(args);
	string exeDir = (exePath) ? exePath.dirName : getcwd();

	gameDir = findGameDir(args, exeDir);
	gameTitle = findGameTitle(gameDir);

	cfgfsMountPoint = cast(string)gameDir.chainPath("custom", "!cfgfs", "cfg").array;

	// check that cfgfs isn't mounted already, and exit if it is
	if (!checkMountBeforeRun(true)) {
		return;
	}

	version (Posix) {
		mkdirRecurse(cfgfsMountPoint);
	}
	version (Windows) {
		mkdirRecurse(cfgfsMountPoint.dirName);
	}

	auto t = task!(() {
		try {
			version (Posix) {
				linkConsoleLog(gameDir, cfgfsMountPoint);
			}

			auto startTime = to!string(Clock.currTime().toUnixTime);

			bool firstRun = true;
			int successCnt = 0;
			int failureCnt = 0;
			while (!gameExited) {
				if (!firstRun) {
					// unmount cfgfs if it was left mounted for some reason
					checkMountBeforeRun(true);
					// fixme: continue on failure?
				}

				string[string] env = [
					"CFGFS_DIR": cfgfsDir,
					"CFGFS_MOUNTPOINT": cfgfsMountPoint,
					"CFGFS_RUN_PID": to!string(getpid()),
					"CFGFS_STARTTIME": startTime,
					"GAMEDIR": gameDir,
					"GAMENAME": gameTitle,
					"GAMEROOT": gameRoot,
					"MODNAME": gameDir.baseName,
					"LC_ALL": null,
					"LD_LIBRARY_PATH": null,
					"LD_PRELOAD": null,
				];
				if (!firstRun) {
					env["CFGFS_RESTARTED"] = "1";
					env["CFGFS_TERMINAL_CLOSED"] = "1";
				}

				auto clock = StopWatch(AutoStart.yes);
				runCfgfsCommand(
					termTitleForGameName(gameTitle),
					[cast(string)cfgfsDir.chainPath(exeName).array, cfgfsMountPoint],
					env
				);
				clock.stop();

				if (clock.peek.total!"msecs" > 5000) { successCnt += 1; failureCnt = 0; }
				if (clock.peek.total!"msecs" > 1000)   successCnt += 1;
				else                                   failureCnt += 1;

				if (failureCnt >= 3) {
					version (Windows) MessageBoxA(null, "cfgfs failed to start three times in a row, giving up.".toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
					break;
				}

				if (gameExited) {
					break;
				}

				Thread.sleep(dur!("msecs")(200));
				firstRun = false;
			}

			// avoid leaving it mounted
			checkMountBeforeRun(true);

			version (Posix) {
				unlinkConsoleLog(gameDir, cfgfsMountPoint);
			}
		} catch (Throwable e) {
			version (Windows) MessageBoxA(null, e.toString().toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
			version (Posix) stderr.writeln(e.toString());
		}
	})();
	t.executeInNewThread();

	bool ready = false;
	int totalWaitMs = 5*1000;
	int waitStepMs = 100;
	int waitedSofarMs = 0;
	while (waitedSofarMs < totalWaitMs) {
		if (t.done) {
			return;
		}

		if (isCfgfsReady(cfgfsMountPoint)) {
			ready = true;
			break;
		}

		Thread.sleep(dur!("msecs")(waitStepMs));
		waitedSofarMs += waitStepMs;
	}

	if (ready) {
		string[] extraArgs = [];
		version (Posix) {
			extraArgs ~= ["-condebug"];
		}
		version (Windows) {
			extraArgs ~= ["+con_logfile", "custom/!cfgfs/cfg/console.log"];
		}
		extraArgs ~= ["+exec", "cfgfs/init"];

		spawnProcess(
			args[1..$]~extraArgs,
			null,
			Config.retainStdin|Config.retainStdout|Config.retainStderr
		).wait();
	} else {
		version (Windows) MessageBoxA(null, "cfgfs seems to be having problems starting. Giving up now.".toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
	}
	gameExited = true;

	version (linux) {
		// this fails if a file is open
		foreach (delay; [0, 333, 333]) {
			Thread.sleep(dur!("msecs")(delay));
			int rv = spawnProcess(
				["fusermount", "-u", cfgfsMountPoint],
				[
					"LD_LIBRARY_PATH": null,
					"LD_PRELOAD": null,
				],
				Config.retainStdin|Config.retainStdout|Config.retainStderr
			).wait();
			if (rv == 0) break;
		}
	}
	version (FreeBSD) {
		// this fails if a file is open
		foreach (delay; [0, 333, 333]) {
			Thread.sleep(dur!("msecs")(delay));
			int rv = spawnProcess(
				["umount", cfgfsMountPoint],
				[
					"LD_LIBRARY_PATH": null,
					"LD_PRELOAD": null,
				],
				Config.retainStdin|Config.retainStdout|Config.retainStderr
			).wait();
			if (rv == 0) break;
		}
	}
	version (Windows) {
		// it just werkz
		foreach (delay; [0, 333, 333]) {
			Thread.sleep(dur!("msecs")(delay));
			HWND term = FindWindowA(null, termTitleForGameName(gameTitle).toStringz);
			if (term != null) {
				PostMessage(term, WM_CLOSE, 0, 0);
				break;
			}
		}
	}

	return;
}

void main(string[] args) {
	// todo: redirect stderr somewhere on windows so writing to it doesn't throw
	version (Posix) stderr.write("=== cfgfs_run ===\n");
	try {
		runMain(args);
	} catch (Throwable e) {
		version (Windows) MessageBoxA(null, e.toString().toStringz(), "cfgfs_run.exe", MB_ICONEXCLAMATION);
		version (Posix) stderr.writeln(e.toString());
	} finally {
		version (Posix) stderr.write("=== /cfgfs_run ===\n");
	}
}
