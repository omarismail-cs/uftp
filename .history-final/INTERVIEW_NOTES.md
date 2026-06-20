# uftp Interview Notes

Talking points, design choices, and bugs from building a UDP file transfer engine in C.

---

## Elevator pitch

"I built a UDP file transfer tool with a custom selective-repeat sliding window and a live terminal UI that visualizes packet flight and buffer gaps. Instead of relying on TCP, I implemented my own sequencing, buffering, selective acks, and retransmission on top of raw datagrams."

---

## Architecture (what to draw on a whiteboard)

```
Sender                          Receiver
------                          --------
read file                       bind UDP port
  |                               |
  v                               v
sliding send window             sliding recv window
  |                               |
  +--- DATA (seq, payload) ------>|
  |<-------- ACK (cum + SACK) ---+
  |                               |
retransmit on timeout           reorder buffer
  |                               |
  v                               v
FIN                             write file in order
```

**Modules:**
- `protocol.h` / `codec.c` — fixed header, CRC32 on header
- `window.c` — send and recv window state
- `net.c` — Winsock / BSD socket wrapper
- `ui.c` — live terminal dashboard (ANSI or ncurses)
- `sender.c` / `receiver.c` — state machines for each side

---

## Key design decisions

### Why UDP instead of TCP?

TCP already solves reliability. The point here is to own the behavior: window size, retransmit policy, ack format, and visibility into what is in flight. On a LAN you can also avoid TCP's congestion assumptions tuned for the internet, though you still need your own flow control.

**Interview line:** "I wasn't trying to beat TCP in all cases. I wanted a minimal transport I could reason about and tune."

### Selective repeat vs go-back-N

**Go-back-N:** receiver drops anything out of order; one loss stalls the whole window.

**Selective repeat:** receiver buffers out-of-order packets; sender retransmits only what is missing.

We chose selective repeat because it handles loss better when the window is large (up to 64 packets). The receiver stores packets in a ring buffer keyed by `seq % window` and delivers in order when gaps fill in.

### Hybrid ACK: cumulative + SACK bitmap

Each ACK carries:
- `cum_ack` — highest consecutive sequence received from the front
- `sack_bitmap` — 64 bits marking which packets above `cum_ack` also arrived

This lets the sender clear in-order progress fast while still learning about out-of-order arrivals without retransmitting everything.

**Interview line:** "Cumulative acks are simple and fast. SACK fills the gap when packet 5 arrives before packet 4."

### Fixed window cap of 64, MSS up to 1400 (runtime tunable)

- Arrays and SACK bitmap are sized at compile time (`UFTP_WINDOW_MAX` = 64, `UFTP_MSS_MAX` = 1400)
- Runtime defaults match those maxes; CLI `-w` (1-64) and `-m` (512-1400) tune behavior without recompile
- Sender announces window + MSS in HELLO; receiver adopts sender values
- 64 slots keeps memory bounded and matches the 64-bit SACK bitmap
- 1400 bytes stays under typical Ethernet MTU after headers, reducing IP fragmentation

### Header CRC only (not payload)

v1 validates the header with CRC32. Payload integrity relies on correct sequencing and file size checks. A production version would add per-chunk or whole-file hashing (planned).

### Cross-platform sockets

`common.h` typedefs `SOCKET` on Windows and `int` on Linux. `net.c` wraps `WSAStartup`, non-blocking I/O, and `sendto`/`recvfrom` behind one API.

### Terminal UI: ANSI fallback + optional ncurses

The UI reads window and stats structs each frame (~30 fps). No extra threads.

- **Default:** ANSI escape codes (alternate screen buffer, colors, cursor hide). Works without extra dependencies in Windows Terminal and most Linux terminals.
- **Optional:** If CMake finds ncurses, compile with `UFTP_HAS_NCURSES` for native color pairs and `erase()`/`refresh()`.

**Sender display:** 64-character row mapped to the send window. Each char is a slot state: pending, in-flight, acked, retransmit.

**Receiver display:** Same layout for the reorder buffer. Highlights gaps (`_`) when a later sequence arrived before the one at `recv_base`.

**Why not only ncurses?** Windows dev environments often lack ncurses installed. Dual backend keeps the project buildable everywhere while still using ncurses when available.

### Tunable window and MSS

- `-w` / `--window` (1-64): max in-flight packets
- `-m` / `--mss` (512-1400): DATA chunk payload size
- Sender announces both in HELLO (`window` field + `seq` field for MSS)
- Receiver adopts sender values; mismatched recv CLI flags log a warning only

Smaller MSS means more packets per file (more header overhead). Smaller window reduces pipelining. On loopback, default (64/1400) was fastest in our bench; smaller settings are still useful for testing buffer behavior.

### Benchmark methodology

`scripts/bench.ps1` runs 16 MB loopback transfers at four configs and prints a markdown table. Loopback numbers are not LAN results. README includes an empty LAN table for manual fills (uftp vs scp vs SMB).

### Whole-file CRC32 verification

- Sender reads the file once before transfer to compute CRC32 (same algorithm as header CRC in `common.c`)
- HELLO and FIN carry the expected hash in `cum_ack` with `UFTP_FLAG_FILE_CRC`
- Receiver updates a rolling hash on every in-order write
- FIN_ACK returns `0` = match, `1` = mismatch
- Chosen over SHA256 to reuse existing code and keep deps zero; good for accidental corruption, not tampering

---

## Bugs we hit and how we fixed them

### 1. Sender hung after handshake on Windows (critical)

**Symptom:** HELLO / HELLO_ACK worked. Data phase never progressed. Sender sat at 0 acks until killed.

**Cause:** `uftp_sock_recv` used `select()` to wait on a non-blocking UDP socket. On Windows, this combination was unreliable. The sender filled its window, then never saw incoming ACKs.

**Fix:** Dropped `select()`. Socket stays non-blocking. `recvfrom` is called in a loop; `WSAEWOULDBLOCK` means no data yet. For timeouts, compare against a deadline and sleep 1 ms between tries.

**Why it matters in an interview:** Shows the difference between blocking, non-blocking, and multiplexed I/O. `select` is not always the right tool on every platform, especially with UDP.

**File:** `src/net.c`

---

### 2. Pointer vs struct mix-up in receiver (compile error)

**Symptom:** `stats->packets_recv++` failed to compile: "invalid type argument of '->'".

**Cause:** `stats` was declared as `uftp_stats_t stats` (value), but some lines used `stats->` as if it were a pointer. `send_ack` correctly takes `uftp_stats_t *`.

**Fix:** Use `stats.field` in `uftp_receiver_run`, pass `&stats` to helpers that need a pointer.

**Interview line:** "Easy mistake when a local struct and a helper expecting a pointer live in the same function."

**File:** `src/receiver.c`

---

### 3. Send window advance could skip sequence numbers

**Symptom:** Potential logic bug during code review, not always visible in happy-path tests.

**Cause:** Early `uftp_send_win_advance` advanced `send_base` whenever a slot was invalid, even if that sequence had not been sent yet (`send_next` still behind).

**Example:** `cum_ack = 1`, `send_base = 1`, `send_next = 1` (only seq 0 sent so far). Old logic could bump `send_base` to 2 and skip seq 1.

**Fix:** Only advance while `send_base < send_next`. Stop if the slot at `send_base` is still a valid, unacked in-flight packet.

```c
while (w->send_base < w->send_next) {
    if (slot valid and unacked at send_base) break;
    w->send_base++;
}
```

**Interview line:** "Window pointers have three regions: acked, in-flight, and not-yet-sent. Advance logic must not cross into not-yet-sent."

**File:** `src/window.c`

---

### 4. Sender sent the whole window before reading any ACKs

**Symptom:** Not a hard failure on loopback, but bad design. 47 packets blasted with zero recv calls in between.

**Cause:** Inner send loop ran until the window was full or EOF, with no ack processing in the same iteration.

**Fix:** Cap sends per round (`sent_this_round < 8`), then drain up to 32 pending ACKs before sending more. Keeps the pipeline full without starving the receive path.

**Interview line:** "A protocol engine needs to interleave send and recv. Pure blast-then-wait works on loopback but falls apart under loss or buffer pressure."

**File:** `src/sender.c`

---

### 5. Broken progress reporting on receiver

**Symptom:** Progress line rarely or never printed during transfer.

**Cause:** Used `if (now % 500 < 50)` as a throttle. That only fires in a narrow time window every 500 ms and is not tied to actual elapsed time.

**Fix:** Track `last_progress` and print every 250 ms of real elapsed time.

**File:** `src/receiver.c`

---

### 6. Sender EOF condition in the send loop

**Symptom:** Risk of spinning or mis-reading after file end.

**Cause:** Loop condition `(!eof || win.send_next < total_seqs)` was confusing and could keep the send path active in odd states.

**Fix:** Simplified to `while (!eof && uftp_send_win_can_send(&win))` with an explicit `send_next >= total_seqs` break.

**File:** `src/sender.c`

---

## Concepts worth explaining fluently

### Sliding window variables

| Side | Variables | Meaning |
|------|-----------|---------|
| Sender | `send_base` | Oldest unacked sequence |
| Sender | `send_next` | Next sequence to assign |
| Receiver | `recv_base` | Next sequence to deliver to the file |

In-flight count = `send_next - send_base`. Must stay <= window size.

### Retransmission

Per-packet timer based on `sent_at_ms`. On timeout, resend that sequence only (selective repeat). RTO doubles up to a cap. Retry count capped at 20.

### Ring buffer indexing

Slots indexed by `seq % window`. Each slot stores the sequence it holds and a `valid` flag to detect wrap-around collisions within the window.

### UDP session flow

1. Sender binds ephemeral port, sends HELLO to receiver port
2. Receiver records sender address from first packet, replies HELLO_ACK
3. DATA flows sender -> receiver; ACK flows back to sender's address
4. FIN / FIN_ACK close the session

No `connect()` call. Address learned from first `recvfrom`.

---

## Test results to mention

**Happy path (0% loss):**
- 64 KB file, loopback, Windows
- 47 data packets, 0 retransmits
- ~80 ms sender-side duration
- SHA256 hash match on input vs output

**Under simulated loss (`--drop 10` on receiver):**
- Transfer still completes, CRC verify OK, file hash matches
- Sender stats show retransmits > 0
- UI shows `!` on sender, `_` on receiver during gaps
- `sim drops` line reports how many datagrams were discarded in `uftp_sock_recv`

Proves selective-repeat recovery works, not just the happy path.

---

## Honest limitations (good to volunteer)

- Whole-file CRC32 only (not per-chunk, not cryptographic)
- No resume after crash
- No auth; anyone on the LAN can connect
- `NACK` message type exists in the enum but is unused (acks handle loss via timeout + SACK)
- Loopback benchmarks only; LAN table in README is a manual template
- Sequence numbers are `uint32_t` with no explicit wrap handling for huge files

---

## Likely interview questions and short answers

**Q: Why not just use TCP?**
A: TCP hides the mechanics. This project is about learning and controlling the transport. TCP is the right default for production; custom UDP makes sense when you need specific latency, fan-out, or visibility.

**Q: What happens if a packet is lost?**
A: Sender times out and retransmits that sequence. Receiver may have later packets buffered; SACK tells the sender what else arrived so it does not resend unnecessarily.

**Q: What happens if the window is too large?**
A: More memory, more in-flight data, higher risk of overrunning receiver or switch buffers. Too small and you underfill the pipe on high-latency links.

**Q: How do you know the file is correct?**
A: Sender computes CRC32 of the whole file before transfer and sends it in HELLO/FIN. Receiver updates a rolling CRC32 as bytes are written to disk and compares at FIN. FIN_ACK reports pass/fail. Stats print `verify: OK` or `FAILED`. Not cryptographic, but catches corruption and truncation.

**Q: How would you test loss?**
A: Built-in `--drop N` flag drops N% of incoming datagrams in `uftp_sock_recv` before the protocol sees them. Put it on the receiver to lose DATA, on the sender to lose ACKs. Also works with Linux `tc netem` for real network loss. UI shows `!` (retransmit) and `_` (gap).

**Q: How does the terminal UI work without blocking the transfer?**
A: The main loop calls `uftp_ui_should_draw()` every ~33 ms. Drawing reads the existing window and stats structs; it does not own any protocol state. Logs go to a ring buffer inside `uftp_ui_t`. On exit, the alternate screen is restored and final stats print to stderr.

**Q: What would you build next?**
A: Resume via persisted ack offset and real LAN benchmark fills for the README table.

**Q: How do you tune throughput?**
A: `-w` for window size, `-m` for chunk size. On loopback we measured ~296 Mbps at defaults vs ~142 Mbps with `-w 16 -m 512` for a 16 MB file. Smaller chunks mean more headers per byte.

---

## One-minute "deep dive" story

"We started with a clean protocol design: fixed headers, numbered DATA packets, ACKs with cumulative and selective ack bits. The first loopback test passed handshake then hung forever. I traced it to `select()` on non-blocking UDP on Windows and replaced it with a poll loop on `recvfrom`. After that, a 64 KB transfer completed in 80 ms with zero retransmits and matching hashes. We fixed window advance logic that could skip sequences, interleaved sends with ack processing, and added a terminal UI that renders the 64-slot window as colored characters so you can watch gaps and retransmits in real time."
