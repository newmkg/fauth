# SystemD implementation hints

This file contains future implementation notes for making `fauth` work with systemd socket activation.

Current implementation mode:

```text
fauth creates Unix socket itself:
socket()
bind()
listen()
accept()
```

Systemd socket activation mode:

```text
systemd creates/listens on Unix socket
fauth receives already-open listening fd
fauth only accepts clients from inherited fd
```

## Goal

Make `fauth` start automatically when a client connects to:

```text
/tmp/myUtils/fauth
```

Then if no active clients exist for 60 seconds, `fauth` exits normally.

The systemd socket unit remains active. On next request, systemd starts `fauth` again.

## Important rule

When using systemd socket activation, C++ code must NOT call:

```cpp
socket()
bind()
listen()
unlink(socketPath)
```

for the socket path.

Systemd already created and is listening on the socket.

The daemon should use inherited fd, normally fd `3`.

## Required code design

Add two server creation modes:

```cpp
Server::manual(path, acceptCb);   // current mode: socket/bind/listen
Server::systemd(acceptCb);        // future mode: inherited fd from systemd
```

Or use constructor flag:

```cpp
Server(ServerMode mode, const char* path, AcceptCb acceptCb);
```

Example enum:

```cpp
enum class ServerMode{
	ManualBind,
	SystemdSocket
};
```

## Detect systemd socket activation

Systemd sets environment variables:

```text
LISTEN_PID
LISTEN_FDS
```

Simple check:

```cpp
bool hasSystemdSocket(){
	const char* fds = std::getenv("LISTEN_FDS");
	return fds && std::atoi(fds) > 0;
}
```

For basic implementation:

```cpp
int listenFd = 3;
```

Because systemd passes first socket at fd `3`.

Better implementation should also check:

```text
LISTEN_PID == getpid()
LISTEN_FDS == 1
```

## Server constructor idea

Manual mode:

```cpp
Server::Server(const char* path, AcceptCb acceptCb){
	::unlink(path);
	fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	setNonBlock(fd);

	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	::listen(fd, 128);

	ev_io_init(&acceptWatcher, acceptCb, fd, EV_READ);
	acceptWatcher.data = this;
	ev_io_start(loop, &acceptWatcher);
}
```

Systemd mode:

```cpp
Server::Server(AcceptCb acceptCb){
	fd = 3;
	setNonBlock(fd);

	ev_io_init(&acceptWatcher, acceptCb, fd, EV_READ);
	acceptWatcher.data = this;
	ev_io_start(loop, &acceptWatcher);
}
```

In systemd mode destructor should NOT unlink socket path.

## Idle timer requirement

Add idle timer to `Server`.

Idea:

```cpp
struct Server{
	int fd{-1};
	ev_io acceptWatcher{};
	ev_timer idleTimer{};
	std::size_t activeClients{};
	bool systemdMode{};
	struct ev_loop* loop{EV_DEFAULT};
};
```

When client is accepted:

```cpp
++activeClients;
stopIdleTimer();
```

When client closes:

```cpp
--activeClients;
if(activeClients == 0)
	startIdleTimer();
```

Idle timer callback:

```cpp
static void idleTimeoutCb(EV_P_ ev_timer* w, int revents){
	auto* s = static_cast<Server*>(w->data);
	ev_break(EV_A_ EVBREAK_ALL);
}
```

Start idle timer:

```cpp
void startIdleTimer(){
	ev_timer_stop(loop, &idleTimer);
	ev_timer_set(&idleTimer, 60.0, 0.0);
	ev_timer_start(loop, &idleTimer);
}
```

Stop idle timer:

```cpp
void stopIdleTimer(){
	ev_timer_stop(loop, &idleTimer);
}
```

Initialize:

```cpp
ev_timer_init(&idleTimer, idleTimeoutCb, 60.0, 0.0);
idleTimer.data = this;
```

If daemon starts and has no active client after startup, start idle timer.

## Client close change

Currently client deletion is local:

```cpp
closeClient(EV_A_ c);
```

For idle tracking, `Client` should know its `Server*`.

```cpp
struct Client{
	Server* server{};
	...
};
```

Then close path should notify server:

```cpp
static void closeClient(EV_P_ Client* c){
	Server* s = c->server;
	delete c;

	if(s && s->activeClients > 0){
		--s->activeClients;
		if(s->activeClients == 0)
			s->startIdleTimer();
	}
}
```

When accepting:

```cpp
Client* c = new Client{...};
c->server = this;
++activeClients;
stopIdleTimer();
```

## systemd unit files

### fauth.socket

```ini
[Unit]
Description=fauth Unix socket

[Socket]
ListenStream=/tmp/myUtils/fauth
SocketMode=0666
RemoveOnStop=true

[Install]
WantedBy=sockets.target
```

### fauth.service

```ini
[Unit]
Description=fauth Firebase Auth verification daemon
Requires=fauth.socket
After=network.target

[Service]
Type=simple
ExecStart=/opt/fauth/fauth.out --systemd
Restart=no

[Install]
WantedBy=multi-user.target
```

## Install commands

```bash
sudo mkdir -p /opt/fauth
sudo cp build/Release/fauth.out /opt/fauth/fauth.out
sudo chmod +x /opt/fauth/fauth.out

sudo cp fauth.socket /etc/systemd/system/fauth.socket
sudo cp fauth.service /etc/systemd/system/fauth.service

sudo systemctl daemon-reload
sudo systemctl enable --now fauth.socket
```

Check status:

```bash
systemctl status fauth.socket
systemctl status fauth.service
```

View logs:

```bash
journalctl -u fauth.service -f
```

## Test

```bash
printf '%s' '{"cmd":"verfToken","projectId":"your-project-id","token":"your-id-token"}' | socat - UNIX-CONNECT:/tmp/myUtils/fauth
```

## Expected behavior

```text
1. fauth.socket is active
2. fauth.service is inactive
3. client connects to /tmp/myUtils/fauth
4. systemd starts fauth.service
5. fauth accepts inherited fd 3
6. fauth processes request and sends response
7. after 60 seconds with no active clients, fauth exits
8. fauth.socket remains active
9. next request starts fauth.service again
```

## Notes

- Do not unlink `/tmp/myUtils/fauth` in systemd mode.
- Do not bind/listen in systemd mode.
- Only accept from inherited fd.
- Keep manual mode also for normal development/testing.
- Idle timeout should exit only when `activeClients == 0`.
