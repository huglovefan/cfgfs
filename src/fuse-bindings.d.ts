declare module "fuse-bindings" {

    import * as fs from "fs";

    interface FuseBuffer extends Buffer {
        length: number;
        parent: FuseBuffer | undefined;
    }

    /**
     * Most of the FUSE api is supported. In general the callback for each op should be called with `cb(returnCode, [value])` where the return code is a number (`0` for OK and `< 0` for errors). See below for a list of POSIX error codes.
     */
    interface FuseMountOps {

        error? (next: (arg1: (...args: any[]) => void) => void): void;

        /**
         * Called on filesystem init.
         */
        init? (mnt: string, cb: (errno: number | null) => void): void;
        init? (cb: (errno: number | null) => void): void;

        /**
         * Called before the filesystem accessed a file.
         */
        access? (path: string, mode: number, cb: (errno: number | null) => void): void;

        /**
         * Called when the filesystem is being stat'ed. Accepts a fs stat object after the return code in the callback.
         */
        statfs? (path: string, cb: (errno: number | null, stat?: Partial<fs.Stats>) => void): void;

        /**
         * Called when a path is being stat'ed. Accepts a stat object (similar to the one returned in `fs.stat(path, cb))` after the return code in the callback.
         */
        getattr? (path: string, cb: (errno: number | null, stat?: Partial<fs.Stats>) => void): void;

        /**
         * Same as `getattr` but is called when someone stats a file descriptor.
         */
        fgetattr? (path: string, fd: number, cb: (errno: number | null) => void): void;

        /**
         * Called when a file descriptor is being flushed.
         */
        flush? (path: string, fd: number, cb: (errno: number | null) => void): void;

        /**
         * Called when a file descriptor is being fsync'ed.
         */
        fsync? (path: string, fd: number, datasync: unknown, cb: (errno: number | null) => void): void;

        /**
         * Same as `fsync` but on a directory.
         */
        fsyncdir? (path: string, fd: number, datasync: unknown, cb: (errno: number | null) => void): void;

        /**
         * Called when a directory is being listed. Accepts an array of file/directory names after the return code in the callback.
         */
        readdir? (path: string, cb: (errno: number | null, names?: string[]) => void): void;

        /**
         * Called when a path is being truncated to a specific size.
         */
        truncate? (path: string, size: number, cb: (errno: number | null) => void): void;

        /**
         * Same as `truncate` but on a file descriptor.
         */
        ftruncate? (path: string, fd: number, size: number, cb: (errno: number | null) => void): void;

        /**
         * Called when a symlink is being resolved. Accepts a pathname (that the link should resolve to) after the return code in the callback.
         */
        readlink? (path: string, cb: (errno: number | null, pathname?: string) => void): void;

        /**
         * Called when ownership of a path is being changed.
         */
        chown? (path: string, uid: unknown, gid: unknown, cb: (errno: number | null) => void): void;

        /**
         * Called when the mode of a path is being changed.
         */
        chmod? (path: string, mode: number, cb: (errno: number | null) => void): void;

        /**
         * Called when the a new device file is being made.
         */
        mknod? (path: string, mode: number, dev: unknown, cb: (errno: number | null) => void): void;

        /**
         * Called when extended attributes is being set (see the extended docs for your platform). Currently you can read the attribute value being set in `buffer` at `offset`.
         */
        setxattr? (path: string, name: string, buffer: FuseBuffer, length: number, offset: number, flags: number, cb: (errno: number | null) => void): void;

        /**
         * Called when extended attributes is being read. Currently you have to write the result to the provided `buffer` at `offset`.
         */
        getxattr? (path: string, name: string, buffer: FuseBuffer, length: number, offset: number, cb: (errno: number | null) => void): void;

        /**
         * Called when extended attributes of a path are being listed. `buffer` should be filled with the extended attribute names as *null-terminated* strings, one after the other, up to a total of `length` in length. (`ERANGE` should be passed to the callback if `length` is insufficient.) The size of buffer required to hold all the names should be passed to the callback either on success, or if the supplied `length` was zero.
         */
        listxattr? (path: string, name: string, buffer: FuseBuffer, length: number, cb: (errno: number | null) => void): void;

        /**
         * Called when an extended attribute is being removed.
         */
        removexattr? (path: string, name: string, cb: (errno: number | null) => void): void;

        /**
         * Called when a path is being opened. `flags` in a number containing the permissions being requested. Accepts a file descriptor after the return code in the callback.
         */
        open? (path: string, flags: number, cb: (errno: number | null, fd?: number) => void): void;

        /**
         * Same as `open` but for directories.
         */
        opendir? (path: string, flags: number, cb: (errno: number | null) => void): void;

        /**
         * Called when contents of a file is being read. You should write the result of the read to the `buffer` and return the number of bytes written as the first argument in the callback. If no bytes were written (read is complete) return 0 in the callback.
         */
        read? (path: string, fd: number, buffer: FuseBuffer, length: number, position: number, cb: (errno: number | null, bytesRead?: number) => void): void;

        /**
         * Called when a file is being written to. You can get the data being written in `buffer` and you should return the number of bytes written in the callback as the first argument.
         */
        write? (path: string, fd: number, buffer: FuseBuffer, length: number, position: number, cb: (errno: number | null, bytesWritten?: number) => void): void;

        /**
         * Called when a file descriptor is being released. Happens when a read/write is done etc.
         */
        release? (path: string, fd: number, cb: (errno: number | null) => void): void;

        /**
         * Same as `release` but for directories.
         */
        releasedir? (path: string, fd: number, cb: (errno: number | null) => void): void;

        /**
         * Called when a new file is being opened.
         */
        create? (path: string, mode: number, cb: (errno: number | null, fd?: number) => void): void;

        /**
         * Called when the atime/mtime of a file is being changed.
         */
        utimens? (path: string, atime: unknown, mtime: unknown, cb: (errno: number | null) => void): void;

        /**
         * Called when a file is being unlinked.
         */
        unlink? (path: string, cb: (errno: number | null) => void): void;

        /**
         * Called when a file is being renamed.
         */
        rename? (src: string, dest: string, cb: (errno: number | null) => void): void;

        /**
         * Called when a new link is created.
         */
        link? (src: string, dest: string, cb: (errno: number | null) => void): void;

        /**
         * Called when a new symlink is created.
         */
        symlink? (src: string, dest: string, cb: (errno: number | null) => void): void;

        /**
         * Called when a new directory is being created.
         */
        mkdir? (path: string, mode: number, cb: (errno: number | null) => void): void;

        /**
         * Called when a directory is being removed.
         */
        rmdir? (path: string, cb: (errno: number | null) => void): void;

        /**
         * Both `read` and `write` passes the underlying fuse buffer without copying them to be as fast as possible.
         */
        destroy? (path: string): void;
    }

    interface FuseMountOpts {
        options?: string[];
        displayFolder?: boolean;
        force?: boolean;
    }

    /**
     * Returns the current fuse context (pid, uid, gid). Must be called inside a fuse callback.
     */
    export function context (): {
        // TODO: do the key names contain "context_"?
        context_pid: number,
        context_uid: number,
        context_gid: number,
    };

    /**
     * Mount a new filesystem on `mnt`. Pass the FUSE operations you want to support as the `ops` argument.
     */
    export function mount (mnt: string, ops: FuseMountOps | FuseMountOpts, opts: FuseMountOpts | FuseMountOps, cb: (err?: Error) => void): unknown;
    export function mount (mnt: string, ops: FuseMountOps | FuseMountOpts, cb: (err?: Error) => void): unknown;

    export function mount (mnt: string, ops: FuseMountOps | FuseMountOpts, opts: FuseMountOpts | FuseMountOps): unknown;
    export function mount (mnt: string, ops: FuseMountOps | FuseMountOpts): unknown;

    /**
     * Unmount a filesystem.
     */
    export function unmount (mnt: string, cb?: (err?: Error) => void): void;

    interface FuseErrno {
        EPERM: -1;
        ENOENT: -2;
        ESRCH: -3;
        EINTR: -4;
        EIO: -5;
        ENXIO: -6;
        E2BIG: -7;
        ENOEXEC: -8;
        EBADF: -9;
        ECHILD: -10;
        EAGAIN: -11;
        ENOMEM: -12;
        EACCES: -13;
        EFAULT: -14;
        ENOTBLK: -15;
        EBUSY: -16;
        EEXIST: -17;
        EXDEV: -18;
        ENODEV: -19;
        ENOTDIR: -20;
        EISDIR: -21;
        EINVAL: -22;
        ENFILE: -23;
        EMFILE: -24;
        ENOTTY: -25;
        ETXTBSY: -26;
        EFBIG: -27;
        ENOSPC: -28;
        ESPIPE: -29;
        EROFS: -30;
        EMLINK: -31;
        EPIPE: -32;
        EDOM: -33;
        ERANGE: -34;
        EDEADLK: -35;
        ENAMETOOLONG: -36;
        ENOLCK: -37;
        ENOSYS: -38;
        ENOTEMPTY: -39;
        ELOOP: -40;
        EWOULDBLOCK: -11;
        ENOMSG: -42;
        EIDRM: -43;
        ECHRNG: -44;
        EL2NSYNC: -45;
        EL3HLT: -46;
        EL3RST: -47;
        ELNRNG: -48;
        EUNATCH: -49;
        ENOCSI: -50;
        EL2HLT: -51;
        EBADE: -52;
        EBADR: -53;
        EXFULL: -54;
        ENOANO: -55;
        EBADRQC: -56;
        EBADSLT: -57;
        EDEADLOCK: -35;
        EBFONT: -59;
        ENOSTR: -60;
        ENODATA: -61;
        ETIME: -62;
        ENOSR: -63;
        ENONET: -64;
        ENOPKG: -65;
        EREMOTE: -66;
        ENOLINK: -67;
        EADV: -68;
        ESRMNT: -69;
        ECOMM: -70;
        EPROTO: -71;
        EMULTIHOP: -72;
        EDOTDOT: -73;
        EBADMSG: -74;
        EOVERFLOW: -75;
        ENOTUNIQ: -76;
        EBADFD: -77;
        EREMCHG: -78;
        ELIBACC: -79;
        ELIBBAD: -80;
        ELIBSCN: -81;
        ELIBMAX: -82;
        ELIBEXEC: -83;
        EILSEQ: -84;
        ERESTART: -85;
        ESTRPIPE: -86;
        EUSERS: -87;
        ENOTSOCK: -88;
        EDESTADDRREQ: -89;
        EMSGSIZE: -90;
        EPROTOTYPE: -91;
        ENOPROTOOPT: -92;
        EPROTONOSUPPORT: -93;
        ESOCKTNOSUPPORT: -94;
        EOPNOTSUPP: -95;
        EPFNOSUPPORT: -96;
        EAFNOSUPPORT: -97;
        EADDRINUSE: -98;
        EADDRNOTAVAIL: -99;
        ENETDOWN: -100;
        ENETUNREACH: -101;
        ENETRESET: -102;
        ECONNABORTED: -103;
        ECONNRESET: -104;
        ENOBUFS: -105;
        EISCONN: -106;
        ENOTCONN: -107;
        ESHUTDOWN: -108;
        ETOOMANYREFS: -109;
        ETIMEDOUT: -110;
        ECONNREFUSED: -111;
        EHOSTDOWN: -112;
        EHOSTUNREACH: -113;
        EALREADY: -114;
        EINPROGRESS: -115;
        ESTALE: -116;
        EUCLEAN: -117;
        ENOTNAM: -118;
        ENAVAIL: -119;
        EISNAM: -120;
        EREMOTEIO: -121;
        EDQUOT: -122;
        ENOMEDIUM: -123;
        EMEDIUMTYPE: -124;
    }

    export function errno <TKey extends keyof FuseErrno> (code: TKey): FuseErrno[TKey];
    export function errno (code: string): -1;

    export const EPERM: FuseErrno["EPERM"];
    export const ENOENT: FuseErrno["ENOENT"];
    export const ESRCH: FuseErrno["ESRCH"];
    export const EINTR: FuseErrno["EINTR"];
    export const EIO: FuseErrno["EIO"];
    export const ENXIO: FuseErrno["ENXIO"];
    export const E2BIG: FuseErrno["E2BIG"];
    export const ENOEXEC: FuseErrno["ENOEXEC"];
    export const EBADF: FuseErrno["EBADF"];
    export const ECHILD: FuseErrno["ECHILD"];
    export const EAGAIN: FuseErrno["EAGAIN"];
    export const ENOMEM: FuseErrno["ENOMEM"];
    export const EACCES: FuseErrno["EACCES"];
    export const EFAULT: FuseErrno["EFAULT"];
    export const ENOTBLK: FuseErrno["ENOTBLK"];
    export const EBUSY: FuseErrno["EBUSY"];
    export const EEXIST: FuseErrno["EEXIST"];
    export const EXDEV: FuseErrno["EXDEV"];
    export const ENODEV: FuseErrno["ENODEV"];
    export const ENOTDIR: FuseErrno["ENOTDIR"];
    export const EISDIR: FuseErrno["EISDIR"];
    export const EINVAL: FuseErrno["EINVAL"];
    export const ENFILE: FuseErrno["ENFILE"];
    export const EMFILE: FuseErrno["EMFILE"];
    export const ENOTTY: FuseErrno["ENOTTY"];
    export const ETXTBSY: FuseErrno["ETXTBSY"];
    export const EFBIG: FuseErrno["EFBIG"];
    export const ENOSPC: FuseErrno["ENOSPC"];
    export const ESPIPE: FuseErrno["ESPIPE"];
    export const EROFS: FuseErrno["EROFS"];
    export const EMLINK: FuseErrno["EMLINK"];
    export const EPIPE: FuseErrno["EPIPE"];
    export const EDOM: FuseErrno["EDOM"];
    export const ERANGE: FuseErrno["ERANGE"];
    export const EDEADLK: FuseErrno["EDEADLK"];
    export const ENAMETOOLONG: FuseErrno["ENAMETOOLONG"];
    export const ENOLCK: FuseErrno["ENOLCK"];
    export const ENOSYS: FuseErrno["ENOSYS"];
    export const ENOTEMPTY: FuseErrno["ENOTEMPTY"];
    export const ELOOP: FuseErrno["ELOOP"];
    export const EWOULDBLOCK: FuseErrno["EWOULDBLOCK"];
    export const ENOMSG: FuseErrno["ENOMSG"];
    export const EIDRM: FuseErrno["EIDRM"];
    export const ECHRNG: FuseErrno["ECHRNG"];
    export const EL2NSYNC: FuseErrno["EL2NSYNC"];
    export const EL3HLT: FuseErrno["EL3HLT"];
    export const EL3RST: FuseErrno["EL3RST"];
    export const ELNRNG: FuseErrno["ELNRNG"];
    export const EUNATCH: FuseErrno["EUNATCH"];
    export const ENOCSI: FuseErrno["ENOCSI"];
    export const EL2HLT: FuseErrno["EL2HLT"];
    export const EBADE: FuseErrno["EBADE"];
    export const EBADR: FuseErrno["EBADR"];
    export const EXFULL: FuseErrno["EXFULL"];
    export const ENOANO: FuseErrno["ENOANO"];
    export const EBADRQC: FuseErrno["EBADRQC"];
    export const EBADSLT: FuseErrno["EBADSLT"];
    export const EDEADLOCK: FuseErrno["EDEADLOCK"];
    export const EBFONT: FuseErrno["EBFONT"];
    export const ENOSTR: FuseErrno["ENOSTR"];
    export const ENODATA: FuseErrno["ENODATA"];
    export const ETIME: FuseErrno["ETIME"];
    export const ENOSR: FuseErrno["ENOSR"];
    export const ENONET: FuseErrno["ENONET"];
    export const ENOPKG: FuseErrno["ENOPKG"];
    export const EREMOTE: FuseErrno["EREMOTE"];
    export const ENOLINK: FuseErrno["ENOLINK"];
    export const EADV: FuseErrno["EADV"];
    export const ESRMNT: FuseErrno["ESRMNT"];
    export const ECOMM: FuseErrno["ECOMM"];
    export const EPROTO: FuseErrno["EPROTO"];
    export const EMULTIHOP: FuseErrno["EMULTIHOP"];
    export const EDOTDOT: FuseErrno["EDOTDOT"];
    export const EBADMSG: FuseErrno["EBADMSG"];
    export const EOVERFLOW: FuseErrno["EOVERFLOW"];
    export const ENOTUNIQ: FuseErrno["ENOTUNIQ"];
    export const EBADFD: FuseErrno["EBADFD"];
    export const EREMCHG: FuseErrno["EREMCHG"];
    export const ELIBACC: FuseErrno["ELIBACC"];
    export const ELIBBAD: FuseErrno["ELIBBAD"];
    export const ELIBSCN: FuseErrno["ELIBSCN"];
    export const ELIBMAX: FuseErrno["ELIBMAX"];
    export const ELIBEXEC: FuseErrno["ELIBEXEC"];
    export const EILSEQ: FuseErrno["EILSEQ"];
    export const ERESTART: FuseErrno["ERESTART"];
    export const ESTRPIPE: FuseErrno["ESTRPIPE"];
    export const EUSERS: FuseErrno["EUSERS"];
    export const ENOTSOCK: FuseErrno["ENOTSOCK"];
    export const EDESTADDRREQ: FuseErrno["EDESTADDRREQ"];
    export const EMSGSIZE: FuseErrno["EMSGSIZE"];
    export const EPROTOTYPE: FuseErrno["EPROTOTYPE"];
    export const ENOPROTOOPT: FuseErrno["ENOPROTOOPT"];
    export const EPROTONOSUPPORT: FuseErrno["EPROTONOSUPPORT"];
    export const ESOCKTNOSUPPORT: FuseErrno["ESOCKTNOSUPPORT"];
    export const EOPNOTSUPP: FuseErrno["EOPNOTSUPP"];
    export const EPFNOSUPPORT: FuseErrno["EPFNOSUPPORT"];
    export const EAFNOSUPPORT: FuseErrno["EAFNOSUPPORT"];
    export const EADDRINUSE: FuseErrno["EADDRINUSE"];
    export const EADDRNOTAVAIL: FuseErrno["EADDRNOTAVAIL"];
    export const ENETDOWN: FuseErrno["ENETDOWN"];
    export const ENETUNREACH: FuseErrno["ENETUNREACH"];
    export const ENETRESET: FuseErrno["ENETRESET"];
    export const ECONNABORTED: FuseErrno["ECONNABORTED"];
    export const ECONNRESET: FuseErrno["ECONNRESET"];
    export const ENOBUFS: FuseErrno["ENOBUFS"];
    export const EISCONN: FuseErrno["EISCONN"];
    export const ENOTCONN: FuseErrno["ENOTCONN"];
    export const ESHUTDOWN: FuseErrno["ESHUTDOWN"];
    export const ETOOMANYREFS: FuseErrno["ETOOMANYREFS"];
    export const ETIMEDOUT: FuseErrno["ETIMEDOUT"];
    export const ECONNREFUSED: FuseErrno["ECONNREFUSED"];
    export const EHOSTDOWN: FuseErrno["EHOSTDOWN"];
    export const EHOSTUNREACH: FuseErrno["EHOSTUNREACH"];
    export const EALREADY: FuseErrno["EALREADY"];
    export const EINPROGRESS: FuseErrno["EINPROGRESS"];
    export const ESTALE: FuseErrno["ESTALE"];
    export const EUCLEAN: FuseErrno["EUCLEAN"];
    export const ENOTNAM: FuseErrno["ENOTNAM"];
    export const ENAVAIL: FuseErrno["ENAVAIL"];
    export const EISNAM: FuseErrno["EISNAM"];
    export const EREMOTEIO: FuseErrno["EREMOTEIO"];
    export const EDQUOT: FuseErrno["EDQUOT"];
    export const ENOMEDIUM: FuseErrno["ENOMEDIUM"];
    export const EMEDIUMTYPE: FuseErrno["EMEDIUMTYPE"];

}
