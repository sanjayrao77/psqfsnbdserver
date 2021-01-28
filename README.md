
# psqfs-nbd-server

## Overview

psqfs-nbd-server creates a read-only pseudo squashfs filesystem from
directories on a mounted filesystem and exports it over tcp via NBD. The tcp
socket can be proxied or encrypted. The server runs in user-space. The remote
client can access the (synthetic) squashfs filesystem with a linux kernel, or
from user-space with other tools.

psqfs-nbd-sever has similar functionality to read-only-NFS but there are some
drastic differences in implementation which make it better-suited for some
applications.

It's similar to http/https in accessing mounted directories but NBD/squashfs
allows easier directory listing and better file caching.


## Quick start

The server can be run from a shell as "*$ ./psqfs-nbd-server .*". This will export
the current directory as the nbd export "." with no security (but writes are
impossible). The server will run in the foreground and print messages about its
operation.

A client can connect to this with "*$ modprobe nbd ; nbd-client -N . 192.168.1.2
3000 /dev/nbd0*", where 192.168.1.2 is the IP of the server and 3000 is the port
printed on startup. Following that with something like "*$ mount -t auto
/dev/nbd0 /mnt/tmp -o ro*" will allow access to the files.

## Building
This is built for linux. It should work on other unix-like systems but might take
some modification.

This builds the server without TLS support.
```bash
make
```

This builds the sever with TLS support but requires GNU TLS headers and libraries installed.
```bash
make psqfs-nbd-server
```

## Use cases

I use this to export my music from my file server. The same export can be
shared over wifi (non-TLS) to an rpi0w at my stereo, as well as over the
internet (TLS encrypted) to wherever I am.

When I used read-only NFS, I didn't want to share it over the internet. UDP
isn't great for this and it's harder to encrypt.

If I were to use http/https, then directory listing, seeking and caching
wouldn't be as efficient as NBD. The httpd wouldn't have to create squashfs
tables but everything else is less efficient in bandwidth, memory and cpu.


## Design details

1. TCP is used exclusively; no UDP
1. The server needs no special kernel modules, linux clients can use nbd.o and
squashfs.o
1. The only libraries used are libc, zlib and (optionally) gnu-tls
1. TLS is supported via gnu-tls for encryption, but not for validation
1. The client needs to reconnect to see changes to the underlying filesystem
1. 5=> This is best for exporting files that don't change often


## Command line options

Most options will be set via a config file. See below for those options.

Usage: psqfs-nbd-server -d -l -v [config file]

### . : (quick start)
-	Share the current directory on a free port with no security.
-	Running "psqfs-nbd-server ." is a quick way to export a directory.
-	This sets the global options "verbose=yes", "debug=yes", "port=3000".
-	It creates an export, "[.]", with "directory=." and "preload=yes".
-	It also attempts to find an open listening port if tcp:3000 is unavailable.
-	This is meant to be used with no other options or config file.

### -d : (debug)
-	The server will run in the foreground and messages will be sent to the
console.
-	This sets the global options "background=no", "debug=yes" and "verbose=yes".
These values can be changed in the config file but stderr output will
only happen with "-d" on the command line.

### -l : (list clients)
-	If the global option "trackclients=yes" is set, then this will list
information about attached clients. This must be run as root or with
the same uid as the running server.
-	This is disabled by default and needs "trackclients=yes" to enable it.

### -v : (verbose)
-	This prints additional information to syslog. This does nothing when
combined with "-d".
-	This only sets the global option "verbose=yes" and can be nullified by a
"verbose=no" in the config file.

### [config file] : explicit location of config file
-	Without this, a config file is sought in /etc/nbd-server/psqfs-config,
~/.psqfsnbd and  ~/.config/psqfs-nbd-server/config .
-	The options and format of the config file are described below.


## Configuration file format

It's a text file. Blank lines and lines starting with # are ignored.
Leading white space is ignored.

The file is broken into sections by "[..]" lines. The special value
"[global]" is used to define global options or defaults. There can
be multiple "[global]" sections.

Exports are specified with a "[..]" section where ".." is the name
of the export.

Example config file:
```
[global]
	user=nobody
	group=nogroup
	clientmax=5
	port=3000
	trackclients=yes
	gziplevel=9
	shorttimeout=5
	denyall=yes
	tlscert=/etc/nbd-server/nbd.cert
	tlskey=/etc/nbd-server/nbd.key
[music]
	# allow localhost to connect
	allownet=127.0.0.1
	# allow LAN to connect
	allownet=192.168.1.0/24
	# allow anyone on IPV4 to connect, if they have TLS
	allowtlsnet=0.0.0.0/0
	# prevent anonymous clients from discovering export name
	listed=no
	# require a password to read export
	keyrequired=yes
	# permit "sesame" to be a valid password.
	# client can request "music]sesame" as the export name
	keypermit=sesame
	# create a synthetic squashfs filesystem from files in /mnt/sdd1/media/music
	directory=/mnt/sdd1/media/music
	# disconnect client after a minute if we're waiting for a reply
	shorttimeout=60
	# disconnect client after an hour if we're waiting for a request
	longtimeout=3600
[movies]
	# create a second export, accessible only from the lan
	allownet=192.168.1.0/24
	# only the lan will see this, so listing is fine
	listed=yes
	directory=/mnt/sdd1/media/movies
```

Connected clients can be listed with "$ ./psqfs-nbd-server -l", run as root or
the same user the server runs under.


## Configuration options

Most server options are set in a config file, either in /etc or in a home directory.

There are a few command line options but they are mostly used for debugging or
querying a running server.

The server supports multiple exports. Each export can be configured independently. It's
also possible to set some options globally and have them affect multiple exports. There
are also options that only apply globally.


## Global options

These options must be contained in a "[global]" or "[generic]" section of the
config file. It's also possible to set export option defaults in [global]
sections but those keywords are listed further below under Global Defaults.

### background=yes/no, default: yes
-	This tells the server to run in the background. Foreground servers can be useful for
users or for debugging.

### clientmax=(number), default: 5
-	The maximum number of simultaneous clients. Each client has its own process.
When the maximum is hit, new connections will be ignored until an existing
client disconnects (or is disconnected).

### debug=yes/no, default: no
-	If yes, the server will output information that could help while debugging
errors. See also "verbose".
-	Messages will be sent to syslog. If you want messages to stderr, use the
"-d" command line option, which does that as well as this.

### port=(number), default: 10809
-	The default TCP port to listen on. The default for the NBD protocol is 10809 but
it may be used already by another server. When running as a user, another port should
be chosen.

### portsearch=(number), default: 0
-	If the specified port (10809 by default) is not available, try the next successive
port (10810, 10811, etc.) and keep going "number" times before we give up.
-	This is not that useful because clients have no easy way to determine the ultimate
port. It can be useful for ad hoc use.

### portwait=(number), default: 0
-	If the specified port is not available, retry for this many seconds before giving up.
A value of 0 will cause the server to fail immediately if the port is occupied. If
another program has already released the port, it can take a short time before the port
is available for the next server (while outstanding packets clear) and setting this
value to around 120 can be useful.
-	In particular, if this server is killed and restarted, the port may be unavailable
for a few minutes. A value of 120 might be prudent.

### shorttimeout=(number), default: 60, also sets the default "shorttimeout" export option
-	A number of seconds of inactivity before a client is disconnected. This value
is used before a client has supplied any credentials. Export settings can
increase the timeout for transactions after an export has been applied.
-	This value is also used as a default for exports. It can be overridden
on a per-export basis.
-	A short value can help avoid denial-of-service stale slots.

### trackclients=yes/no, default: no
-	If "trackclients=yes", then the server will export information about attached
clients. This is stored in the processes' environment and is only readable by
processes with the server's uid and root.
-	If enabled, connected clients can be listed with "psqfs-nbd-server -l".

### tlscert=(filename)
-	A filename for a PEM format certificate file. This is required for TLS and
unused for non-TLS. TLS can be used by a client if tlscert= and tlskey= are
supplied.
-	After you have a private key, this can be generated by "certtool
--generate-self-signed --load-privkey YOURNAME.key --outfile YOURNAME.cert".

### tlskey=(filename)
-	A filename for a PEM format private key file. This is required for TLS and
unused for non-TLS. TLS can be used by a client if tlscert= and tlskey= are
supplied.
-	This can be generated by "certtool --generate-privkey --outfile
YOURNAME.key".

### tlsrequired=yes/no, default: no
-	If "tlsrequired=yes", then all clients have to start TLS when they connect.
By effect, the "tlsrequired" option in exports is then meaningless as TLS is
necessary before that matters.
-	If "tlsrequired=no", then export listing is possible without TLS and individual
exports can decide whether TLS is required via the "tlsrequired" export option.

### verbose=yes/no, default: no
-	This will print more information to syslog about what the server is doing.


## Export keywords

To define an export, a line of the form "[exportname]" should exist in the
config file. Options for the export will follow that line. The names "generic"
and "global" are invalid for an export. Also, an export name can't contain the
']' or '\0' characters. UTF8 is fine.


### 4kpad=yes/no, This is an action, not a setting
-	If the line "4kpad=yes" is found, the server will add 0s to the exported
image until the so-far specified size is a multiple of 4096. This is not
necessary after a "directory=" or "overlay=" line. It's useful after adding
a literal block with "filename_ro=" and when the client is a kernel.

### allownet=IP[/number], inherits from global's allownet values
-	If the export is "denyall=yes", this authorizes an IP range to access this export.
-	Examples: "allownet=127.0.0.1", "allownet=::1", "allownet=192.168.1.0/24"

### allowtlsnet=IP[/number], inherits from global's allowtlsnet values
-	If the export is "denyall=yes", this authorizes an IP range to acces this export
but ONLY if the client has enabled TLS. This is useful for non-TLS clients on a LAN
and TLS-only clients on a WAN.
-	Example:
```
[vimfiles]
	directory=/usr/share/vim
	tlsrequired=no
	denyall=yes
	allownet=127.0.0.1
	allownet=192.168.1.0/24
	allowtlsnet=10.0.0.0/8
```
-	See "allownet" for additional examples.

### denyall=yes/no, default: no, inherits from global's denyall
-	Access can be restricted by IP if denyall=yes. If denyall=no, then all IPs can access
the export.

### directory=(directory)
-	No more than one directory should be specified for an export. It is not required.
-	This tells the server to read the given directory and export it as a squashfs filesystem
when the export is requested. Access will be read-only.
-	There is no attempt to follow symlinks outside of this directory.
-	To add additional files or directories outside of this directory, you can use the "overlay"
keyword to merge other files and directories into the synthetic squashfs filesystem.
-	If (directory) is blank, a blank root directory will be made in the squashfs filesystem.
This is useful when combined with "overlay". It's also implied if an "overlay" is specified
without a preceding "directory".

### filename_ro=(filename)
-	This appends the file or block device data specified by (filename) into the export's image.
-	If you want 4096-byte padding, see the "4kpad" keyword.
-	Multiple files can be added this way. The client will see a single block device as a
concatenation of all the filename, 4kpad and directory blocks specified.
-	A shared lock will be attempted with flock(). If the lock succeeds, it should
prevent subsequent read-write access to programs asking for locks. If the lock attempt
fails, reading will still proceed.
-	Example: (not particularly recommended; "overlay" example below is easier)
```
[backup-partitions]
	filename_ro=/var/loopbackfs
	4kpad=yes
	directory=/etc
	filename_ro=/dev/sdc1
```

### group=(groupname)
-	Specify a group to setgid() to after binding listening socket.
-	When running as non-root, this will probably create an error.
-	See "user" for more information.

### gziplevel=(number), default 6, inherits from global's "gziplevel"
-	(number) should be 0 through 9, corresponding to zlib's level.
-	If "gziplevel=0" is specified, any sqfs images created with "directory" will
be completely uncompressed. This is faster for the server to build and is
necessary for clients that don't support compression.
-	Higher values take more cpu cycles to make the sqfs image. On the other
hand, the image will be smaller so less memory is needed to store it and
less bandwdith in needed to satisfy requests.

### keepalive=yes/no, default ?
-	If "keepalive=yes", the client's tcp socket will be set to SO_KEEPALIVE
so the connection looks alive to the network. Note that "longtimeout"
doesn't see this as traffic.

### keypermit=KEY, inherits from global's keypermit values
-	The value "KEY" will be accepted as a valid key to access this export.
See "keyrequired".

### keyrequired=yes/no, default: no
-	Access to an export can be restricted by a key, like a password. If
"keyrequired=yes", then the client must send "exportname]KEY" (note the ']') to
access the export. When configuring the clients, just enter "exportname]KEY"
as the name of the export; clients don't need to know about the key structure.
-	If everything else matches (IP, TLS, "listed=yes"), a client can query the
existence of the export without a valid key.
-	This can be useful to password-protect an export. Multiple keys can be defined
with "keypermit".
-	Example:
```
[mymusic]
	directory=/home/me/music
	keyrequired=yes
	# clients can access this as "mymusic]mypassword" or "mymusic]guestpassword"
	keypermit=mypassword
	keypermit=guestpassword
```

### longtimeout=(number), default: 7\*24\*60\*60 (1 week), inherits from global's "longtimeout"
-	Clients will be disconnected after (number) seconds of idleness when waiting for
a command.

### listed=yes/no, default: yes, inherits from global's "listed"
-	An export that is "listed=yes" will be shown to authorized clients which ask
for an export list. Note that an IP or TLS requirement ("denyall","tlsrequired")
may prevent a client from asking for an export list.
-	This also authorizes a client from knowing that a key is required for an
export. If an otherwise-authorized client asks for an export that is
"listed=yes" and requires a key, they will receive a message stating a
key is required. The client could then try again with a key. With "listed=no",
a client is not given information about key requirements as that would tell
the client the export exists.

### maxfiles=(number), default: 0 (no max), inherits from global's "maxfiles"
-	When scanning a directory, if (number) is exceeded, the server abandons the
scan with an error.
-	While this prevents infinite loops, such loops should already be detected by
an inode collision detection. This might detect a loop sooner.
-	To specify no maximum, 0 can be entered.

### nodelay=yes/no, default: yes, inherits from global's "nodelay"
-	Set the tcp option TCP\_NODELAY. This might reduce the server's latency at
the cost of efficiency.

### shorttimeout=(number), default: 60 (1 minute), inherits from global's "shorttimeout"
-	Disconnect a client after (number) seconds when expecting a reply. This should
be set higher for slow or unreliable links.

### tlsrequired=yes/no, default: no, inherits from global's "tlsrequired"
-	Require the client to have enabled TLS encryption before asking for this
export. If the client has enabled TLS, the export name and key are transmitted
while TLS is active.
-	It's still possible for a non-TLS client to send the export name and key
unencrypted. Clients should be pre-configured to request TLS if TLS is wanted.

### overlay=(realpath) -> (fakepath)
-	Create the specified (fakepath) in the squashfs filesystem. Any needed
directories will be created. The actual data will come from (realpath). The
overlay will be ignored if (fakepath) already exists in the filesystem.
-	if (realpath) is a symlink or a succession of symlinks, the target will be
considered rather than a symlink.
-	If (realpath) is a directory, it will be scanned and the contents will be
added into (fakepath). If (realpath) is a block device, the device's contents
will be added as a regular file.
-	If (realpath) is a block device and you want an actual block device added,
use the "overlayraw" keyword instead.
-	If you wish to specify a "directory" line, it should precede the first
overlay.
-	Note that the delimiter is " -> ", literally, space-hyphen-greaterthan-space.
Anything else will not be parsed correctly.
-	Example:
```
[myfiles]
	# /mnt/sdb1/public/media does not need to exist
	directory=/mnt/sdb1/public
	overlay=/mnt/sdb1/music -> /media/music
	overlay=/mnt/sdb1/movies -> /media/movies
	overlay=/dev/sdb1 -> /partitions/sdb1
	overlay=/dev/sdb2 -> /partitions/sdb2
```

### overlayraw=(realpath) -> (fakepath)
-	This is like the "overlay" keyword but it adds "realpath" literally.
-	If "realpath" is a symbolic link, it will add the symbolic link rather
than the target.
-	If "realpath" is a block device, it will add a block device file rather than
the data of the device.
-	If you'd rather add the target of the symbolic link or the data of
the device file, use the "overlay" keyword instead.
-	Note that the delimiter is " -> ", literally, space-hyphen-greaterthan-space.
Anything else will not be parsed correctly.
-	See the "overlay" keyword for more information.
	

### preload=yes/no, default: no, inherits from global's "preload"
-	If "preload=yes", the squashfs image will be created when the server first
starts and before it accepts any clients.
-	This has the advantage that multiple clients sharing the same export will
share the same memory and the image only has to be built once.
-	The main disadvantage is that changes in the underlying filesystem will not
be included in the exported image until the server is restarted. Another
disadvantage is that the filesystem overhead will occupy system memory even if
the export isn't being used.
-	On the other hand, this is very useful for debugging.

### user=(username)
-	Specify a user to setuid() to after binding listening socket.
-	Along with "group", the specified user.group combination will need
to be able to read any files specified by the exports.
-	When running as non-root, this will probably create an error.
-	In general, privilege reduction is a great idea. However, care must
be made that exported files can be accessed after setuid/setgid.


## Global defaults

These values can be set in a [global] section as defaults for exports.
They have no effect per se. Note that some global options, listed
above, can also be used to set defaults for exports. Also note that
multiple [global] sections are allowed.

### allownet=IP[/number]
-	This sets defaults for the "allownet" export option. Any IP range
specified here will apply to all exports until an "allowreset=yes" is listed.

### allowtlsnet=IP/number]
-	This sets defaults for the "allowtlsnet" export option. Any IP range
specified here will apply to all exports until an "allowreset=yes" is listed.

### allowreset=yes/no
-	An "allowreset=yes" line clears the "allownet" and "allowtlsnet" global
default values. Any new "allownet" and "allowtlsnet" values which follow this
line will be applied to following exports only.
-	Example:
```
[global]
	allownet=127.0.0.1
	allownet=192.168.1.0/24
	denyall=yes
[myexport]
	directory=/mnt/public
[global]
	allowreset=yes
	allownet=127.0.0.1
[myexport2]
	directory=/mnt/private
```
	
### denyall=yes/no
-	This sets defaults for the "denyall" export option. Exports following this
line
	
### gziplevel=(number), number in [0..9]
-	This sets the defaults for the "gziplevel" export option.

### keepalive=yes/no
-	This sets the default on the "keepalive" export option.

### keypermit=KEY
-	This sets the defaults for the "keypermit" export option. Any key
listed here will be valid for every export until a "keyreset=yes" line.

### keyrequired=yes/no
-	This sets the default for the "keyrequired" export option.

### keyreset=yes/no
-	This clears all the global "keypermit" defaults. Following exports will
not inherit any previously specified "keypermit" values. Any keys specified
after this will only be inherited by following exports.
-	Example:
```
[global]
	keyrequired=yes
	keypermit=admin
[export1]
	keypermit=sesame
	directory=/mnt/export1
[global]
	keyreset=yes
[export2]
	keypermit=sesame
	# "admin" is no longer a valid key
	directory=/mnt/export2
```

### longtimeout=(number)
-	This sets the default for the "longtimeout" export option.

### listed=yes/no
-	This sets the default for the "listed" export option.

### maxfiles=(number)
-	This sets the default for the "maxfiles" export option.

### nodelay=yes/no
-	This sets the default for the "nodelay" export option.

### overlay=(realpath) -> (fakepath)
-	This sets the defaults the "overlay" export option. If you want to
overlay the same file(s) in many of your exports, this lets you do
that.
-	These will be the defaults until an "overlayreset=yes" line clears
them.
-	Note that the delimiter is " -> ", literally, space-hyphen-greaterthan-space.
Anything else will not be parsed correctly.

### overlayreset=yes/no
-	This clears all the global "overlay" defaults. Following exports will
not inherit any previously specified "overlay" values. Any overlays
specified after this will only be inherited by following exports.

### preload=yes/no
-	This sets the default for the "preload" export option.
