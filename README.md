# DNS Tunneling Assignment

This repository contains a simple DNS tunneling client and server written in C.

Files
- `dns-server.c` — UDP DNS server. Extracts Base64 from the first label, decodes it and prints the decoded message. Encodes reply text into A records (ASCII bytes in IPv4 octets).
- `dns-client.c` — DNS client. Base64-encodes messages (includes trailing NUL), sends DNS A queries for `<b64>.<domain>`, and decodes replies from A records.

Build and run (Linux / WSL / VM)

1. Compile:

```bash
gcc -o server dns-server.c -Wall -Wextra
gcc -o client dns-client.c -Wall -Wextra
```

2. Run server (non-root test): use port 53 (requires root) or use a non-privileged port like 5353.

Run on port 53 (requires sudo/root):

```bash
sudo ./server 53
```

Or run as a normal user on an unprivileged port (example: 5353):

```bash
./server 5353
```

3. In another terminal, run the client pointing to the server. Example (when server is on localhost port 53):

```bash
./client 127.0.0.1 53
```

Or if using port 5353 (non-root):

```bash
./client 127.0.0.1 5353
```

You should see the client print the encoded messages and the decoded server replies, and the server print the encoded query name and decoded data.

Dig test

If the server runs on port 53 on localhost:

```bash
dig @localhost aGkA.google.com
dig @localhost aGVsbG8A.google.com
```

If the server runs on a non-standard port (e.g., 5353), you can run dig like this:

```bash
dig @127.0.0.1 -p 5353 aGkA.google.com
```

Safety and assumptions

- The code is intended for lab/testing use on your own VMs. Don't use it on networks where you don't have permission.
- The client includes a trailing NUL byte in Base64 encoding so encoded values match the examples in the assignment.
- The client includes a trailing NUL byte in Base64 encoding so encoded values match the examples in the assignment.

Windows / PowerShell notes
- If you're on Windows, you can build/run using MSYS2/Mingw (gcc) or use WSL/Ubuntu. The repo includes helpers:
	- `run_test.sh` — builds and runs server/client on port 5353 (on Linux/WSL).
	- `run_test.ps1` — PowerShell helper (requires `gcc` in PATH, e.g., from MSYS2).

Security note
- Run these programs only on your own lab VMs or networks where you have permission.