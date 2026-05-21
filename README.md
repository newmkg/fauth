# fauth

A lightweight C++23 Firebase Authentication verification daemon using libev and Unix domain sockets for fast local IPC.

## Features

- Firebase ID token verification
- Unix domain socket IPC
- libev-based event loop
- JSON request/response using Boost.JSON
- Lightweight daemon-style design

## Requirements

- Linux
- GCC 15+ or C++23 compatible compiler
- libev
- Boost.JSON

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install g++ libev-dev libboost-json-dev
```

## Build

```bash
chmod +x scripts/makeGpp.sh
./scripts/makeGpp.sh
```

Output:

```text
build/Release/fauth.out
```

## Run

```bash
chmod +x scripts/startServer.sh
./scripts/startServer.sh
```

Default Unix socket path:

```text
/tmp/myUtils/fauth
```

## Request format

Send JSON to the Unix socket:

```json
{
  "cmd": "verfToken",
  "projectId": "your-firebase-project-id",
  "token": "firebase-id-token"
}
```

## Test with socat

```bash
printf '%s' '{"cmd":"verfToken","projectId":"your-project-id","token":"your-id-token"}' | socat - UNIX-CONNECT:/tmp/myUtils/fauth
```

Pretty print:

```bash
printf '%s' '{"cmd":"verfToken","projectId":"your-project-id","token":"your-id-token"}' | socat - UNIX-CONNECT:/tmp/myUtils/fauth | jq .
```

## Test with netcat

```bash
printf '%s' '{"cmd":"verfToken","projectId":"your-project-id","token":"your-id-token"}' | nc -U -q 0 /tmp/myUtils/fauth
```

## Response format

Success:

```json
{
  "success": true,
  "idtoken": {}
}
```

Error:

```json
{
  "success": false,
  "error": "error message"
}
```

## Notes

- Do not commit private keys, tokens, `.env`, or Firebase service account JSON files.
- This daemon uses one-request-one-response socket flow.
- Client should send request, shutdown write side, then read response until EOF.

## License

MIT License
