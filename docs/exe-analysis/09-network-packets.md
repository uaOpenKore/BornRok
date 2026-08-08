# Subsystem 9 — Network & packet dispatch

TCP client to the RO server trio (login → char → map). Winsock is bound **dynamically**
and the in-game protocol is decoded by a single 306-case dispatcher.

## Winsock init & the send/recv hook — `FUN_00418db0` @ `0x00418db0`

1. `WSAStartup(0x0101)` (else `"Failed to load Winsock library"`).
2. `LoadLibraryA("ws2_32.dll")` → `DAT_006ee668`.
3. `GetProcAddress` **send** → `DAT_006ee6e4`, **recv** → `DAT_006ee66c`. If either is
   null it substitutes an internal stub (`DAT_0065644a` / `DAT_00656444`) and pops
   `"GetProcAddress(send/recv) Failed — Module Hooking Error"`.

All socket send/recv go through these **function pointers**, not direct imports — a
deliberate late-binding layer (defeats naive IAT hooks / supports packet obfuscation).

## Connection object — `FUN_00419480` @ `0x00419480`

Lazy singleton returning the connection/output manager **`&DAT_006ee670`** (CRagConnection).
- Socket: `socket(AF_INET, SOCK_STREAM, 0)` set **non-blocking** via
  `ioctlsocket(FIONBIO)`; `connect(sockaddr, 0x10)` — `FUN_00418af0` @ `0x00418af0`.
- The client opens **three connections in sequence** (login server → char server →
  map/zone server), each `ip:port` taken from the clientinfo/char-select reply. The mode
  state ids `{7,8,0xB,0x13}` mark "connected" phases (Subsystem 2 keep-alive).
- A UDP socket (`socket(AF_INET, SOCK_DGRAM, 0)` @ `0x0059f…`) exists for auxiliary
  queries (e.g. ping/latency).

## Receive → frame → dispatch

Each frame the connection `recv`s (via the hooked pointer) into a receive buffer, then
**frames** packets: the leading `uint16` command id selects a size from the client's
**packet-length table** (fixed length) or, for variable packets, the following `uint16`
is the total length. Each complete packet is handed to the dispatcher.

### Master in-game dispatcher — `FUN_00579900` @ `0x00579900`

The main **ZC_** handler: a **306-case `switch`** on the packet id covering spawn/despawn,
movement (`ZC_NOTIFY_*`), chat, `ZC_NOTIFY_ACT 0x8A` (attack/sit/pick → combat visuals),
skills, status changes, inventory/equip/storage, party/guild/friends, NPC dialogs, quests,
effects (`0x1F3`), etc. Called from the connection process loop (`0x00559b00` region,
call site `~0x556xxx`). Large stack scratch (~25 KB) is used to assemble UI-bound
structures (dialog text, lists).

Earlier phases use smaller dispatchers: **`FUN_00645650` @ `0x00645650`** (~108 cases)
and `FUN_006445f0` @ `0x006445f0` handle the login/char-server replies (account/char
list, server list, accept/refuse). The uaRO-custom packet **`0x2A2`** is handled here and
in the main dispatcher.

## Outgoing packets

Built through the connection manager: `FUN_00419480` (get conn) → begin a packet with id
(`FUN_004192f0`) → append fields / flush (`FUN_004191b0` / `FUN_00418a90`) → `send` via
the hooked pointer. Example: the 12 s **char-server ping `0x187`** (`FUN_0059fe10`,
Subsystem 2).

## Encryption

The string `"No Packet Encryption..."` (`0x00579…`) shows the client supports an optional
**packet-obfuscation / shuffle keying**; this build runs without it. (Protocol ground
truth for this server: OpenKore uOK210, serverType 8, ver 20, custom `0x22A/0x22B/0x22C` —
see memory `openkore-uok210-protocol-reference` and `docs/packet-audit.md`.)

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Winsock init + hook | `0x00418db0` | WSAStartup, resolve send/recv ptrs |
| Connect (TCP) | `0x00418af0` | non-blocking socket+connect |
| Connection singleton | `0x00419480` | CRagConnection `DAT_006ee670` |
| **Main ZC dispatcher** | `0x00579900` | 306-case in-game handler |
| Login/char dispatcher | `0x00645650` | ~108-case pre-game replies |
| Login/char dispatcher 2 | `0x006445f0` | account/server list |
| send ptr / recv ptr | `DAT_006ee6e4` / `DAT_006ee66c` | hooked ws2_32 |
| Packet begin/append | `0x004192f0` / `0x004191b0` | build outgoing |
