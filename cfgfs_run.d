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

import core.runtime;
import core.thread.osthread;
import core.time;
import core.stdc.stdlib;

import core.sys.windows.windows;

string findGameDir(string[] args) {
	for (int i = 1; i < args.length-1; i++) {
		if (args[i] == "-game") {
			return args[i+1].absolutePath;
		}
	}
	throw new StringException("couldn't parse command line (missing -game parameter)");
}

string findGameTitle(string gameDir) {
	auto gameinfo = File(gameDir.chainPath("gameinfo.txt"), "r");
	foreach (line; gameinfo.byLine) {
		if (auto m = line.matchAll("^[\0-\x20]*game[\0-\x20]+\"(.*)\"[\0-\x20]*$")) {
			return to!string(m.front[1]);
		}
	}
	throw new StringException("couldn't parse gameinfo.txt");
}

bool isCfgfsReady(string mountpoint) {
	auto path = mountpoint.chainPath("cfgfs", "buffer.cfg");
	try {
		auto f = File(path, "r");
		return true;
	} catch (Exception e) {
		return false;
	}
}

version (Windows)
string[] getTerminalCommand(string title, string[] cmd) {
	return [
		"C:\\cygwin64\\bin\\mintty",
//			"--hold", "always",
			"--title", title,
			"--exec",
	] ~ cmd;
}

version (linux)
string[] getTerminalCommand(string title, string[] cmd) {
	return [
		"xterm",
			"-title", title,
			"-e",
			"sh", "-c", `[ ! -e env.sh ] || . ./env.sh; exec "$@"`, "--",
	] ~ cmd;
}

version (linux)
void linkConsoleLog(string gameDir, string mountPoint) {
	string log = to!string(gameDir.chainPath("console.log").array);
	string target = to!string(mountPoint.chainPath("console.log").array);
	if (exists(log)) {
		log.rename("console.log.old");
	}
	symlink(target, log);
}
version (linux)
void unlinkConsoleLog(string gameDir, string mountPoint) {
	string log = to!string(gameDir.chainPath("console.log").array);
	if (log.isSymlink) {
		log.remove();
	}
}

shared bool wantExit;

void runMain(string[] args_) {

	auto t = task!((string[] args) {

		string cfgfsDir = thisExePath().dirName;
		string gameRoot = getcwd();

		string gameDir = findGameDir(args);
		string gameTitle = findGameTitle(gameDir);

		string cfgfsMountPoint = to!string(gameDir.chainPath("custom", "!cfgfs", "cfg").array);

		version (linux) {
			linkConsoleLog(gameDir, cfgfsMountPoint);
		}

		while (!wantExit) {
			auto p = spawnProcess(
				getTerminalCommand("cfgfs (%s)".format(gameTitle), [to!string(cfgfsDir.chainPath("cfgfs").array), cfgfsMountPoint]),
				[
					"CFGFS_DIR": cfgfsDir,
					"CFGFS_MOUNTPOINT": cfgfsMountPoint,
					"CFGFS_RUN_PID": to!string(getpid()),
					"CFGFS_STARTTIME": to!string(Clock.currTime().toUnixTime),
					"GAMEDIR": gameDir,
					"GAMENAME": gameTitle,
					"GAMEROOT": gameRoot,
					"LC_ALL": null,
					"LD_LIBRARY_PATH": null,
					"LD_PRELOAD": null,
				],
				Config.retainStdin|Config.retainStdout|Config.retainStderr,
				cfgfsDir);
			int rv = p.wait();

			// fixme: mintty exits immediately
			version (Windows) {
				break;
			}

			Thread.sleep(dur!("msecs")(200));
		}

		version (linux) {
			unlinkConsoleLog(gameDir, cfgfsMountPoint);
		}

	})(args_);

	t.executeInNewThread();

	string gameDir = findGameDir(args_);
	string cfgfsMountPoint = to!string(gameDir.chainPath("custom", "!cfgfs", "cfg").array);

	while (true) {
		if (t.done) {
			return;
		}

		if (isCfgfsReady(cfgfsMountPoint)) {
			break;
		}

		Thread.sleep(dur!("msecs")(100));
	}

	string[] extraArgs = [
		"+exec", "cfgfs/init",
	];
	version (linux) {
		extraArgs ~= ["-condebug"];
	}
	version (Windows) {
		extraArgs ~= ["+con_logfile", "custom/!cfgfs/cfg/console.log"];
	}

	auto p = spawnProcess(
		args_[1..$]~extraArgs,
		null,
		Config.retainStdin|Config.retainStdout|Config.retainStderr,
		".");
	int rv = p.wait();

	wantExit = true;
	version (linux) {
		execute(["fusermount", "-u", cfgfsMountPoint]);
	}
	version (Windows) {
		// todo
	}

	return;
}

void main(string[] args) {
	try {
		runMain(args);
	} catch (Throwable e) {
		version (Windows) MessageBoxA(null, e.toString().toStringz(), null, MB_ICONEXCLAMATION);
		stderr.writeln(e.toString());
	}
}
