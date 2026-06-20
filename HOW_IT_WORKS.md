# How uftp works (plain English)

This is the friendly version. Read this first if the main README feels too technical.

---

## What this program does

**uftp copies a file from one computer to another over the network.**

You run `recv` on the machine that should receive the file. You run `send` on the machine that has the file. When it's done, both sides print stats and the receiver checks that the file wasn't corrupted.

Under the hood it:

1. Cuts the file into numbered pieces (chunks)
2. Sends those pieces over **UDP** (fast, no built-in reliability)
3. Adds its own reliability: "did you get piece 3?", resend if not, buffer out-of-order pieces
4. Shows you that process live in the terminal (optional)
5. Fingerprints the whole file at the end to make sure it arrived correctly

It does **not** encrypt the file, pause and resume later, or replace normal tools like `scp` for everyday use. It's a working demo of how reliable transfer can be built on top of UDP.

---

## Terms explained

Short definitions for words you'll see in the README, stats output, and UI.

| Term | Plain English |
|------|----------------|
| **UDP** | A way to send small messages (datagrams) over the network. Fast and simple. Unlike TCP, it does not guarantee delivery or order — packets can be lost or arrive jumbled. |
| **TCP** | The usual "reliable" internet transport. It handles loss, reordering, and flow control for you. uftp deliberately does not use it so the mechanics are visible. |
| **Packet / datagram** | One UDP message on the wire: a small header plus optional payload (your file bytes). |
| **Chunk** | One slice of the file that fits in a single DATA packet's payload. |
| **Sequence number (seq)** | The chunk's ID: 0, 1, 2, 3… Used to put pieces back in order and to know which ones are missing. |
| **Port** | A number (e.g. `9000`) that identifies which program on a machine should receive UDP traffic. Like an apartment number on a building address (the IP). |
| **IP address** | The machine's network address (e.g. `192.168.1.5` on a LAN, or `127.0.0.1` for "this same machine"). |
| **Loopback** | Sending to `127.0.0.1` — traffic stays on your own PC. Good for testing; not the same as copying across a real LAN. |
| **LAN** | Local area network — e.g. two laptops on the same Wi‑Fi or Ethernet. |
| **Handshake** | Short back-and-forth before data: sender says "I want to send this file," receiver says "OK." (HELLO / HELLO_ACK.) |
| **Window** | How many packets the sender is allowed to have "in flight" at once before waiting for confirmations. Default 64; tunable with `-w`. |
| **MSS** | Maximum chunk size in bytes per DATA packet (default 1400). Tunable with `-m`. Bigger chunks = fewer packets; smaller chunks = more overhead. |
| **In flight** | Sent but not yet confirmed by the receiver. |
| **ACK (acknowledgment)** | Receiver → sender: "I got this." uftp ACKs report both the longest consecutive run received and a bitmap of extra pieces. |
| **Cumulative ACK** | "I have everything from the start through sequence N in order." |
| **SACK (selective ACK)** | Bitmap saying which individual packets *above* that cumulative point also arrived early. Avoids resending pieces you already have. |
| **Selective repeat** | Strategy: only resend the specific missing packets; keep out-of-order ones in a buffer. (Alternative: go-back-N would discard early arrivals and resend a whole window.) |
| **Reorder buffer** | Memory on the receiver where early-arriving chunks wait until the missing one shows up, then everything is written to disk in order. |
| **Retransmit** | Sender sends the same chunk again because no ACK arrived in time. |
| **Timeout / RTO** | How long the sender waits before assuming a packet was lost. Grows with exponential backoff on repeated tries. |
| **Gap** | A hole in the sequence — e.g. you have chunk 5 but not chunk 4 yet. Shown as `_` on the receiver UI. |
| **FIN / FIN_ACK** | Polite shutdown at the end of the transfer. |
| **CRC32** | A checksum — a number computed from all file bytes. Same idea as a quick fingerprint. Sender and receiver compare at the end; not cryptography, just "did the bytes change?" |
| **`verify: OK`** | Receiver's rolling CRC matched the sender's pre-computed CRC. File likely intact. |
| **`--no-ui`** | Same transfer without the live dashboard; plain progress text. |
| **`--drop`** | Test mode: randomly throw away some incoming packets so you can watch retransmits work. |
| **Throughput** | How fast data moved, usually shown in Mbps (megabits per second) in the stats. |

---

## The big picture

You have a file on computer A. You want it on computer B over your local network.

Normal tools (browser downloads, file sharing, `scp`) use **TCP**. TCP is like sending mail with tracking built in: the post office handles lost letters, reorders them, and slows down if the network is busy.

**uftp uses UDP instead.** UDP is like throwing postcards out a window. No built-in tracking. Faster to set up, but *you* have to handle "did it arrive?" yourself.

We built that tracking layer from scratch. That's the whole project.

---

## What you actually run

Two commands, two terminals:

```
Terminal 1:  uftp recv 9000 received.bin     ← waits for a file
Terminal 2:  uftp send 192.168.1.5 9000 photo.jpg   ← sends a file
```

- `recv` = "I'm listening on port 9000, save whatever arrives as received.bin"
- `send` = "Send this file to that IP on that port"

That's it. Everything else happens automatically behind those two commands.

---

## What happens step by step

Think of the file as a book split into 47 numbered pages (chunks). Each page is one UDP packet.

### 1. Handshake (hello)

Sender: "Hey, I want to send a 65 KB file. 47 pages total."

Receiver: "OK, I'm ready. Save it here."

If this fails, nothing else starts. We tested this part early; it works.

### 2. Sending pages (data)

Sender throws pages out as fast as the network allows. Up to **64 pages in flight** at once (the "window").

Each page has a number: 0, 1, 2, 3...

Receiver gets pages. They might arrive out of order (page 5 before page 4). That's fine. The receiver **holds them in a buffer** and waits for the missing one.

### 3. "Got it" messages (acks)

Receiver tells sender: "I have pages 0 through 3. I also have page 5, but I'm still missing page 4."

Sender crosses off pages it doesn't need to resend. It **only resends what's actually missing**.

### 4. If something goes missing (timeout)

If the sender throws page 4 out and hears nothing back for 100ms, it throws page 4 again. And again, with longer waits each time.

In our tests on your machine: **zero resends needed**. The network was fast and clean.

### 5. Goodbye (fin)

Sender: "That was the last page."

Receiver: "Confirmed. File saved."

Both sides print stats (speed, bytes, retransmits).

---

## What the terminal UI is showing you

When you run without `--no-ui`, you get a live dashboard. Here's how to read it.

### Sender screen

```
send window  base=12  next=47  in-flight=8
  ++++++++++++>>>>>>>>........................
```

Each character is one slot in the 64-packet window:

| Char | Meaning |
|------|---------|
| `.` | Not sent yet |
| `>` | Sent, waiting for confirmation |
| `+` | Confirmed received |
| `!` | Being resent (something went wrong) |

Reading left to right: older packets on the left, newer on the right. You watch `>` turn into `+` as acks come back. If you see `!`, a packet was lost and got resent.

### Receiver screen

```
recv buffer  base=12  gaps=0
  ++++++++++++=##............................
```

| Char | Meaning |
|------|---------|
| `.` | Empty slot, nothing arrived yet |
| `=` | This is the next page we need to write to disk |
| `#` | Arrived early, sitting in the buffer |
| `_` | **Gap** — we're missing a page but got later ones |
| `+` | Already written to the file |

The `_` character is the interesting one. That's selective-repeat in action: page 15 arrived but page 12 is still missing.

### Throughput line

```
throughput  [000456788888888888888888888888888888]  6.2 Mbps
```

A simple speed graph. The numbers 0-8 are bar height. Shows how fast data is moving right now.

### Event log

Plain messages: "handshake ok", "gap detected at seq 4", "retransmit seq 4". The story of what happened.

---

## What we built so far (project timeline)

| Stage | What we did | Status |
|-------|-------------|--------|
| 1 | Basic UDP packets, file split into chunks | Done |
| 2 | Reliability: acks, retransmits, reorder buffer | Done |
| 3 | Fixed Windows bug (sender hung after hello) | Done |
| 4 | Terminal UI to visualize packets | Done |
| 5 | Simulated packet loss (`--drop`) to test retransmits | Done |
| 6 | Whole-file CRC32 verification | Done |
| 7 | CLI tuning flags (`-w`, `-m`) and loopback benchmarks | Done |
| 8 | Resume, LAN benchmark fills | Not yet |

Stages 1–7 are done. The tool transfers files on loopback, verifies them with CRC32, supports tuning and loss simulation, and can show a live UI.

---

## The Windows bug (why the first tests failed)

**What you saw:** Handshake worked, then nothing. Sender frozen.

**What was wrong:** The code used `select()` to wait for incoming acks. On Windows with UDP, that call was unreliable and never woke up.

**What we did:** Stopped using `select()`. Just try to read in a loop, sleep 1ms if nothing there, try again.

**Lesson:** Network code behaves differently on Windows vs Linux. Always test both.

---

## File map (what each file does)

You don't need to read all of these. This is just a map.

| File | One sentence |
|------|-------------|
| `main.c` | Parses your command (`send` or `recv`) |
| `sender.c` | The send side: read file, throw packets, handle acks, resend |
| `receiver.c` | The receive side: listen, buffer packets, write file in order |
| `window.c` | Tracks which packets are in flight / buffered |
| `net.c` | Opens UDP sockets, sends and receives bytes |
| `codec.c` | Packs and unpacks packet headers |
| `ui.c` | Draws the terminal dashboard |
| `fileio.c` | Reads and writes the actual file on disk |
| `protocol.h` | Defines packet types (HELLO, DATA, ACK, etc.) |

---

## Common questions

**Why not just use TCP?**
We could. The point is to understand and control what TCP does automatically. Also fun to watch packets fly in the UI.

**Is it faster than normal file sharing?**
On loopback we've measured hundreds of Mbps for large files. Real LAN numbers vary and we haven't published comparisons to scp or SMB. The main point is understanding and watching the protocol, not guaranteed speed.

**Is it safe to use on the internet?**
No. No encryption, no authentication. Local network only for now.

**What does `--no-ui` do?**
Same transfer, but simple text progress instead of the dashboard. Good for scripts.

**What does `--drop 10` do?**
Pretends 10% of incoming packets never arrived. Use it to prove retransmits work.

```powershell
# Receiver drops DATA packets (sender has to resend)
build\uftp.exe --no-ui --drop 10 recv 9000 received.bin
build\uftp.exe --no-ui send 127.0.0.1 9000 myfile.bin
```

You should see `retransmits` > 0 in the stats, but the file should still be correct and `verify: OK` at the end.

**How do we know the file is correct?**
Before sending, the sender fingerprints the whole file with CRC32 (like a quick serial number for the content). The receiver counts the same way as it writes bytes to disk. At the end they compare. Stats show `verify: OK` or `verify: FAILED`.

**What's next?**
Resume if interrupted. Optional: run the same file transfer on two real machines and note throughput in the README benchmarks section.

### Tuning flags (`-w` and `-m`)

- **`-w` / `--window`** — how many packets can be in flight at once (1-64, default 64)
- **`-m` / `--mss`** — how many bytes per chunk (512-1400, default 1400)

Only the sender needs these flags. The receiver learns the values from the HELLO handshake.

```powershell
build\uftp.exe --no-ui -w 16 -m 512 send 127.0.0.1 9000 myfile.bin
```

Smaller window = less pipelining. Smaller MSS = more packets, more header overhead per byte. On loopback with a 16 MB file, defaults were fastest in our tests (~296 Mbps vs ~142 Mbps for w=16 mss=512).

Run benchmarks yourself:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bench.ps1
```

---

## Try it yourself

```powershell
# Build
gcc -std=c11 -Wall -Wextra -Iinclude -o build/uftp.exe src/*.c -lws2_32

# Terminal 1
build\uftp.exe recv 9000 received.bin

# Terminal 2
build\uftp.exe send 127.0.0.1 9000 somefile.jpg
```

Watch the sender window fill with `>` then turn to `+`. When it's done, compare the files or check the stats at the bottom.
