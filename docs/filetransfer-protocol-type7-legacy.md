# VNC File Transfer Wire Protocol — Exact Specification for a libvncclient-based C++ Client

**Purpose:** implement a client-side file transfer feature in C++ on top of *libvncclient*
(LibVNCServer project).

**Source tree (read-only reference, all citations refer to it):**

```
C:\src\libvncserver
```

Citations use the form `path:line` or `path:line-line` relative to that root. Every
behavioural claim below is backed by a citation; claims the tree cannot answer are
explicitly marked **AMBIGUOUS** or **UNVERIFIED**.

**Terminology:** “type-7 protocol” = the bidirectional `rfbFileTransfer` (message type 7,
UltraVNC-style) protocol described in `include\rfb\rfbproto.h:1020-1093` and implemented
server-side in `src\libvncserver\rfbserver.c`. This is the protocol this document
specifies in full. The separate *TightVNC-1.x extension* (message types 130–136) is
summarised in §4 only, because it is a different protocol family.

---

## 0. Quick reference constants

| Constant | Value | Source |
|---|---|---|
| `rfbFileTransfer` (message type byte) | `7` | `include\rfb\rfbproto.h:422` |
| `sz_rfbFileTransferMsg` (header size) | `12` | `rfbproto.h:1037` |
| `sz_rfbBlockSize` (max raw chunk) | `8192` | `rfbproto.h:1088` |
| `rfbFileTransferVersion` | `2` (compile-time constant only, **never transmitted**) | `rfbproto.h:1039`; grep shows no other use in the tree |

Content types (`rfbproto.h:1042-1055`):

| # | Name | Meaning |
|---|---|---|
| 1 | `rfbDirContentRequest` | client→server directory/drives request |
| 2 | `rfbDirPacket` | server→client directory entry / list terminator |
| 3 | `rfbFileTransferRequest` | client→server “send me this file” |
| 4 | `rfbFileHeader` | server→client file header; client→server receive-ack/reject |
| 5 | `rfbFilePacket` | one chunk (either direction) |
| 6 | `rfbEndOfFile` | end of transfer (either direction) |
| 7 | `rfbAbortFileTransfer` | abort (either direction); also legacy permission probe |
| 8 | `rfbFileTransferOffer` | client→server “I want to send you this file” |
| 9 | `rfbFileAcceptHeader` | server→client accept/reject of the offer |
| 10 | `rfbCommand` | client→server command (mkdir/delete/rename) |
| 11 | `rfbCommandReturn` | server→client command result |
| 12 | `rfbFileChecksums` | delta-transfer checksums — **unimplemented, see §3.3** |
| 13 | *(undefined — do not use)* | gap between 12 and 14 (`rfbproto.h:1054-1055`) |
| 14 | `rfbFileTransferAccess` | permission grant/deny reply (see §2d) |

`contentParam` values: request params `rfbRDirContent=1`, `rfbRDrivesList=2`,
`rfbRDirRecursiveList=3`, `rfbRDirRecursiveSize=4` (`rfbproto.h:1058-1061`); answer params
`rfbADirectory=1` … `rfbADirRecursiveSize=11` (`rfbproto.h:1064-1074`); command params
`rfbCDirCreate=1` … `rfbCDirRename=6` (`rfbproto.h:1077-1082`); errors
`rfbRErrorUnknownCmd=1`, `rfbRErrorCmd=0xFFFFFFFF` (`rfbproto.h:1085-1086`).

---

## 1. Byte order / field encoding of `rfbFileTransferMsg`

### 1.1 Structure and wire layout

```c
/* include\rfb\rfbproto.h:1026-1037 */
typedef struct _rfbFileTransferMsg {
    uint8_t type;			/* always rfbFileTransfer */
    uint8_t contentType;  /*  See defines below */
    uint8_t contentParam;/*  Other possible content classification (Dir or File name, etc..) */
    uint8_t pad;         /* It appears that UltraVNC *forgot* to Swap16IfLE(contentParam) */
    uint32_t size;		/*  FileSize or packet index or error or other  */
/*  uint32_t sizeH;		 Additional 32Bits params to handle big values. Only for V2 (we want backward compatibility between all V1 versions) */
    uint32_t length;
    /* followed by data char text[length] */
} rfbFileTransferMsg;

#define sz_rfbFileTransferMsg	12
```

Wire layout (offsets from start of message):

| Offset | Size | Field | Wire encoding |
|---|---|---|---|
| 0 | 1 | `type` | `0x07` |
| 1 | 1 | `contentType` | raw byte, **never swapped** |
| 2 | 1 | `contentParam` | raw byte, **never swapped** |
| 3 | 1 | `pad` | raw byte, send `0`; server ignores it entirely |
| 4 | 4 | `size` | **big-endian (network order) uint32** |
| 8 | 4 | `length` | **big-endian (network order) uint32** |
| 12 | `length` | `data` | raw bytes |
| 12+length | 4 | `sizeH` | present **only** after `rfbFileHeader` (server→client, success) and `rfbFileTransferOffer` (client→server) — see §1.4 |

### 1.2 Endianness evidence

Sending side (server) — `src\libvncserver\rfbserver.c:1315-1323`:

```c
rfbBool rfbSendFileTransferMessage(rfbClientPtr cl, uint8_t contentType, uint8_t contentParam, uint32_t size, uint32_t length, const char *buffer)
{
    rfbFileTransferMsg ft;
    ft.type = rfbFileTransfer;
    ft.contentType = contentType;
    ft.contentParam = contentParam;
    ft.pad          = 0; /* UltraVNC did not Swap16LE(ft.contentParam) (Looks like it might be BigEndian) */
    ft.size         = Swap32IfLE(size);
    ft.length       = Swap32IfLE(length);
```

Receiving side (server) — `rfbserver.c:2789-2801`:

```c
    case rfbFileTransfer:
        if ((n = rfbReadExact(cl, ((char *)&msg) + 1,
                              sz_rfbFileTransferMsg - 1)) <= 0) { ... rfbCloseClient(cl); return; }
        msg.ft.size         = Swap32IfLE(msg.ft.size);
        msg.ft.length       = Swap32IfLE(msg.ft.length);
        rfbProcessFileTransfer(cl, msg.ft.contentType, msg.ft.contentParam, msg.ft.size, msg.ft.length);
```

Note that only `size` and `length` are swapped; `contentType`/`contentParam` are consumed
as bytes and `msg.ft.pad` is **never read or checked** (only the 4 listed fields are
passed on, `rfbserver.c:2800`).

What `Swap32IfLE` means — `include\rfb\rfb.h:774-783`:

```c
extern char rfbEndianTest;
#define Swap32IfLE(l) (rfbEndianTest ? Swap32(l) : (l))
```

`rfbEndianTest` is `1` exactly on little-endian hosts — `src\libvncserver\main.c:52-56`:

```c
#ifdef LIBVNCSERVER_WORDS_BIGENDIAN
char rfbEndianTest = (1==0);
#else
char rfbEndianTest = (1==1);
#endif
```

⇒ `Swap32IfLE` byte-swaps on LE hosts and not on BE hosts ⇒ **the value on the wire is
always big-endian**, i.e. standard RFB network byte order. `size` and `length` are
therefore big-endian in **both** directions.

**Client-side macro** — `include\rfb\rfbclient.h:64-71`:

```c
#define rfbClientSwap32IfLE(l) \
    (*(char *)&client->endianTest ? ((((l) >> 24) & 0x000000ff) | ... ) : (l))
```

The macro depends on an identifier named `client` in scope, and on
`client->endianTest` (`rfbclient.h:260`). The public constructor
`rfbGetClient()` hardcodes it: `client->endianTest = 1;` —
`src\libvncclient\vncviewer.c:297` (calloc) and `:309`. **Consequence:** if you build the
`rfbClient` yourself with `calloc` instead of `rfbGetClient()`, `endianTest` is 0, the
macro degenerates to “no swap”, and you will emit *little-endian* values that the server
will misread. Either use `rfbGetClient()`, set `client->endianTest = 1` yourself, or
serialize BE manually with shifts (recommended for clarity).

(The macro therefore swaps unconditionally on every host in practice; on a theoretical
big-endian host libvncclient would produce little-endian `size`/`length` — **UNVERIFIED /
theoretical**, no BE host is exercised in this tree.)

### 1.3 The `pad` quirk

`type/contentType` are single bytes; the original UltraVNC struct evidently had a 16-bit
`contentParam` slot that UltraVNC never byte-swapped. Evidence:

- `rfbproto.h:1030` — `uint8_t pad;  /* It appears that UltraVNC *forgot* to Swap16IfLE(contentParam) */`
- `rfbserver.c:1321` — `ft.pad = 0; /* UltraVNC did not Swap16LE(ft.contentParam) (Looks like it might be BigEndian) */`

Practical rule: **wire byte[2] = contentParam, wire byte[3] = 0.** Because `contentParam`
values are all < 256 this reproduces the little-endian `[param,0]` image of a 16-bit
field. Send `pad = 0`; ignore `pad` on receive (this server does).

### 1.4 The extra 4-byte `sizeH` trailer (v2 big-file field)

`rfbproto.h:1032` documents a commented-out `uint32_t sizeH` *inside* the struct comment
block (“Additional 32Bits params to handle big values. Only for V2”), yet this
implementation does **not** place it in the header. It appends it **after the data
payload**, and only for two content types:

**Server → client, `rfbFileHeader` (download), success only** — `rfbserver.c:1853-1876`:

```c
        retval = rfbSendFileTransferMessage(cl, rfbFileHeader, 0, (cl->fileTransfer.fd==-1 ? -1 : statbuf.st_size), length, buffer);

        if (cl->fileTransfer.fd==-1)
        {
            free(buffer);
            return retval;            /* <-- error header: NO sizeH trailer */
        }
        ...
        /* TODO: finish 64-bit file size support */
        sizeHtmp = 0;        
        LOCK(cl->sendMutex);
        if (rfbWriteExact(cl, (char *)&sizeHtmp, 4) < 0) { ... }
```

⇒ On success the stream is `[12B header][data][4 raw bytes, always 0x00000000]`.
On failure (`size == 0xFFFFFFFF`) **no trailer follows**.

**Client → server, `rfbFileTransferOffer` (upload)** — the server *always* reads 4 bytes
after the payload — `rfbserver.c:1909-1931`:

```c
        if ((buffer = rfbProcessFileTransferReadBuffer(cl, length))==NULL) return FALSE;
        ...
        /* Need to read in sizeHtmp */
        if ((n = rfbReadExact(cl, (char *)&sizeHtmp, 4)) <= 0) { ... rfbCloseClient(cl); ... }
        sizeHtmp = Swap32IfLE(sizeHtmp);
```

⇒ The client **must** write exactly 4 bytes after every `rfbFileTransferOffer` payload or
the server blocks/desyncs. The server byte-swaps it on read (`Swap32IfLE` ⇒ expects
big-endian) but then never uses it (`sizeHtmp` is dead after line 1931; the offer’s `size`
is stored as `cl->fileTransfer.fileSize = size`, `rfbserver.c:1952`). The server *writes*
the trailer raw/unswapped (`rfbserver.c:1869`) — indistinguishable from BE because it is
always 0.

> **AMBIGUOUS (interop):** whether original UltraVNC/TightVNC put `sizeH` in the header
> (as the struct comment at `rfbproto.h:1032` suggests) or after the payload (as this
> implementation does) cannot be determined from this tree — no third-party server source
> is present. Against a LibVNCServer-based server, use the payload trailer exactly as
> specified here. Against other servers this is **UNVERIFIED**.

### 1.5 Directory-entry sub-structure `RFB_FIND_DATA` (opposite endianness!)

Directory entries are **binary Windows `WIN32_FIND_DATA`-shaped records** (“UltraVNC uses
Windows Structures”, `rfbserver.c:1354-1355`), defined at `rfbserver.c:1359-1375`:

```c
typedef struct {
    uint32_t dwFileAttributes;
    RFB_FILETIME ftCreationTime;
    RFB_FILETIME ftLastAccessTime;
    RFB_FILETIME ftLastWriteTime;
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t dwReserved0;
    uint32_t dwReserved1;
    uint8_t  cFileName[ MAX_PATH ];      /* MAX_PATH = 260, rfbserver.c:1357 */
    uint8_t  cAlternateFileName[ 14 ];
} RFB_FIND_DATA;
```

These fields are **little-endian (“Intel Order”) on the wire, regardless of host
endianness** — they are produced with `Swap32IfBE`, e.g. `rfbserver.c:1519-1528`:

```c
            win32filename.dwFileAttributes = Swap32IfBE(RFB_FILE_ATTRIBUTE_NORMAL);
            if (S_ISDIR(statbuf.st_mode))
                win32filename.dwFileAttributes = Swap32IfBE(RFB_FILE_ATTRIBUTE_DIRECTORY);
            win32filename.ftCreationTime.dwLowDateTime = Swap32IfBE(statbuf.st_ctime);   /* Intel Order */
            ...
            win32filename.nFileSizeLow = Swap32IfBE(statbuf.st_size); /* Intel Order */
```

(`Swap32IfBE` = `rfbEndianTest ? (s) : Swap32(s)`, `rfb.h:781-783` ⇒ little-endian on the
wire in both cases; the Windows branch copies native little-endian `WIN32_FIND_DATA`
fields verbatim, `rfbserver.c:1505-1517`.)

Field offsets (fixed part = 44 bytes):

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `dwFileAttributes` | LE; `0x10` = directory, `0x80` = normal (`rfbserver.c:1377-1384`) |
| 4 | 8 | `ftCreationTime` | Windows: FILETIME; Unix: `st_ctime` (epoch s) in low dword, high = 0 (`rfbserver.c:1522-1523`) |
| 12 | 8 | `ftLastAccessTime` | same, `st_atime` (`rfbserver.c:1524-1525`) |
| 20 | 8 | `ftLastWriteTime` | **mtime**; Windows: FILETIME (100 ns since 1601); Unix: `st_mtime` epoch seconds in low dword (`rfbserver.c:1526-1527`) |
| 28 | 4 | `nFileSizeHigh` | LE, 0 on Unix (`rfbserver.c:1529`) |
| 32 | 4 | `nFileSizeLow` | LE (`rfbserver.c:1528`) |
| 36 | 4 | `dwReserved0` | 0 |
| 40 | 4 | `dwReserved1` | 0 |
| 44 | … | `cFileName` | file name, see below |

Transmitted length — `rfbserver.c:1541`:

```c
                nOptLen = sizeof(RFB_FIND_DATA) - MAX_PATH - 14 + strlen((char *)win32filename.cFileName);
```

The buffer is zeroed first (`rfbserver.c:1503`). With a normal ABI (alignof 4 ⇒
`sizeof(RFB_FIND_DATA) = 320` after tail padding) this yields
`length = 46 + strlen(name)`: fixed 44 bytes + name + `NUL` + one spare zero byte.
*(`sizeof` is computed at compile time; 320 vs. a packed 318 — giving `44 + strlen(name)`
with no terminator — is an ABI property **derived**, not measured in-tree: **UNVERIFIED**.)
**Client rule that works for both:** parse the name as bytes `[44, length)`, stop at the
first `NUL` (or at `length` if none), and never assume a fixed entry size — always use
the received `length`.

> **Tree-vs-UltraVNC note:** the legacy string decoration constants
> `rfbDirPrefix "[ "` / `rfbDirSuffix " ]"` (`rfbproto.h:1091-1092`) and
> `rfbZipDirectoryPrefix "!UVNCDIR-\0"` (`rfbproto.h:1089-1090`) exist **only as
> definitions** — grep finds no use anywhere else in the tree. This server never emits
> `"[ dir ]"`/`"[ file ]"`-style textual entries and never transfers zipped directories.
> Whether original UltraVNC produced such strings is **UNVERIFIED** from this tree.

---

## 2. Exact message sequences

Notation: fields shown as `type, contentType, contentParam, pad, size, length` plus
payload. All multi-byte integers big-endian unless stated. Every `length` must equal the
exact number of payload bytes written — **several server handlers never read the payload,
so a wrong `length` desynchronises or kills the connection** (see §2.6).

### 2a. Remote directory listing (and drive list)

**Request (list)** — handled at `rfbserver.c:1775-1782`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 1 (rfbDirContentRequest) / 1 (rfbRDirContent) / 0` |
| size | `0` (unused by server) |
| length + data | `strlen(path)`, path bytes, e.g. `"C:/folder/sub"` or `"C:\folder\sub"` |

The server reads exactly `length` bytes (`rfbserver.c:1779`) and NUL-terminates the
buffer at `buffer[length]` (`rfbserver.c:1612`), so a trailing NUL in the payload is
optional. Path translation: a leading `"C:"` is stripped and `\`→`/` on non-Windows
servers; anything else gets `$HOME` prepended (`rfbserver.c:1400-1421`).

**Response stream** — produced by `rfbSendDirContent` (`rfbserver.c:1436-1575`):

1. **Path echo** — `rfbserver.c:1485`:
   `rfbSendFileTransferMessage(cl, rfbDirPacket, rfbADirectory, 0, length, buffer)`
   ⇒ `7, 2, 1, 0, size=0, length=len(path), data = <the exact path string you sent>`.

2. **One message per entry** — `rfbserver.c:1541-1545`:
   ```c
   nOptLen = sizeof(RFB_FIND_DATA) - MAX_PATH - 14 + strlen(...cFileName);
   if (rfbSendFileTransferMessage(cl, rfbDirPacket, rfbADirectory, 0, nOptLen, (char *)&win32filename)==FALSE)
   ```
   ⇒ `7, 2, 1, 0, size=0, length=nOptLen, data = RFB_FIND_DATA record` (§1.5).
   **Note: `contentParam` is `rfbADirectory` (1) for files too** — this implementation
   never sends `rfbAFile` (2) in listings; distinguish directory vs. file via
   `dwFileAttributes & 0x10`. (Whether original UltraVNC used `rfbAFile` for files is
   **AMBIGUOUS**; a robust client should accept both.)
   Hidden entries (leading `.`) are skipped except `".."` (`rfbserver.c:1539`); entries
   failing `stat()` are silently dropped (`rfbserver.c:1501`).

3. **End-of-list marker** — `rfbserver.c:1574`:
   ```c
   return rfbSendFileTransferMessage(cl, rfbDirPacket, 0, 0, 0, NULL);
   ```
   ⇒ `7, 2, 0, 0, size=0, length=0`, **no payload**. (`contentParam == 0` and
   `length == 0` is the terminator; entries always have `length ≥ 44`.)

**Directory open failure** — `rfbserver.c:1477-1482`: the server sends exactly one
message `7, 2, 1, 0, 0, length=0` (an “entry” with no data) and **no terminator**.
Client rule: if the first response after the request has `length == 0`, treat the listing
as failed/empty and stop.

**Drive list** — request `7, 1, 2 (rfbRDrivesList), 0, size=0, length=0` (no payload; the
handler at `rfbserver.c:1748-1774` reads nothing — sending data here desyncs).
Response — `rfbserver.c:1767-1772`:

```c
            filename2[0]='C';
            filename2[1]=':';
            filename2[2]='l';
            filename2[3]=0;
            filename2[4]=0;
            retval = rfbSendFileTransferMessage(cl, rfbDirPacket, rfbADrivesList, 0, 5, filename2);
```

⇒ `7, 2, 3 (rfbADrivesList), 0, size=0, length=5, data = "C:l\0\0"` — this server always
answers with the fake single drive (no `#ifdef WIN32`). The intended multi-drive format
is documented at `rfbserver.c:1752-1762`: `"C:l\0D:c\0…Z:n\0\0"` where the char after
`X:` encodes the type — `DRIVE_FIXED='l'`, `DRIVE_REMOVABLE='f'`, `DRIVE_CDROM='c'`,
`DRIVE_REMOTE='n'` — terminated by an extra NUL.

**Recursive list / recursive size** (`contentParam` 3 and 4): defined
(`rfbproto.h:1060-1061`) but **not implemented** — the inner switch at
`rfbserver.c:1747-1783` has no case for them: no payload is read (desync if you send
data) and **no response is sent at all**. Do not use.

### 2b. Download a file (server → client)

**(1) Request** — handled `rfbserver.c:1807-1877`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 3 (rfbFileTransferRequest) / 0 / 0` |
| size | **`1` if this client can zlib-decompress packet payloads, else `0`** — `rfbserver.c:1845-1846`: `/* The viewer supports compression if size==1 */ cl->fileTransfer.compressionEnabled = (size==1);` |
| length + data | `strlen(path)`, path bytes (no timestamp needed; the server appends its own in the reply) |

**(2) File header** (server→client) — `rfbserver.c:1853`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 4 (rfbFileHeader) / 0 / 0` |
| size | file size (uint32), or **`0xFFFFFFFF` on open/stat failure** |
| length + data | `"<path you sent>,<MM/DD/YYYY HH:MM>"` — the server appends `","` and a timestamp, `rfbserver.c:1830-1838`: `strftime(timespec, sizeof(timespec), "%m/%d/%Y %H:%M",gmtime(&statbuf.st_ctime))` (UTC of `st_ctime`). On failure the payload is the path *without* timestamp. |
| trailer | **success only:** 4 raw bytes `00 00 00 00` (`sizeH`, §1.4). On `size == 0xFFFFFFFF` there is **no** trailer. |

`size` truncates files > 4 GiB to 32 bits (server note: `/* TODO: finish 64-bit file size
support */`, `rfbserver.c:1866`; `sizeH` is always sent as 0) — rely on `rfbEndOfFile`,
not on `size`, for completion; treat `size` as advisory (**UNVERIFIED** against other
servers).

**(3) Client ack / reject** — handled `rfbserver.c:1879-1894`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 4 (rfbFileHeader) / 0 / 0` |
| size | **`0xFFFFFFFF` = reject/abort** (server closes its fd and stops, `rfbserver.c:1881-1886`, no reply); any other value starts the stream (comment: “size > 0 or not (size = -1)”, `rfbserver.c:1880`; the value is otherwise ignored). Send the file size you saw in (2). |
| length | **0 — this handler reads no payload** (`rfbserver.c:1879-1894`); any payload desyncs the stream |

On ack the server immediately sends the first chunk and then pushes further chunks
asynchronously from its event loop — `rfbserver.c:1892-1894`
(`/* Starts the transfer! */ cl->fileTransfer.sending=1; return rfbSendFileTransferChunk(cl);`;
pumps at `src\libvncserver\main.c:571,587` and `src\libvncserver\sockets.c:482,577`).

**(4) Data packets** (server→client), N × — `rfbserver.c:1693-1715`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 5 (rfbFilePacket) / 0 / 0` |
| size | **compression flag: `0` = raw payload, `1` = zlib-compressed payload** (see §3.2) |
| length + data | raw: ≤ 8192 file bytes; compressed: zlib stream of ≤ 8192 raw bytes |

**(5) End / error** (server→client):

- `7 / 6 (rfbEndOfFile) / 0 / 0, size=0, length=0` — sent on normal EOF
  (`rfbserver.c:1675`).
- `7 / 7 (rfbAbortFileTransfer) / 0 / 0, size=0, length=0` — sent on server-side read
  error (`rfbserver.c:1687`).

**Client-side abort** (any time, incl. during the header phase):
`7 / 7 (rfbAbortFileTransfer) / 0 / 0, size=0, length=0` — server closes the fd and
replies **nothing** if a transfer was active (`rfbserver.c:2002-2012`); see §2d for the
surprise reply when *no* transfer is active.

No packet numbering exists on the wire despite the generic `rfbproto.h:1031` comment
(“FileSize or packet index or error or other”) — TCP ordering is assumed; `size` doubles
as the compression flag for `rfbFilePacket`.

### 2c. Upload a file (client → server)

There is **no second file header** in the type-7 protocol; the offer *is* the header.

**(1) Offer** — handled `rfbserver.c:1902-1956`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 8 (rfbFileTransferOffer) / 0 / 0` |
| size | file size (uint32, low 32 bits) |
| length + data | path; optionally `"<path>,<MM/DD/YYYY HH:MM>"` — the server splits at the **last** comma (`rfbserver.c:1912-1918`) but discards the time (`szFileTime` is never used afterwards; “TODO: Delta Transfer”, `rfbserver.c:1936-1937`). **Recommendation: send the bare path** (see quirk below). |
| trailer | **mandatory 4 bytes** `sizeH` (big-endian convention per `rfbserver.c:1931`); send `00 00 00 00`. Server ignores the value but blocks if the 4 bytes are missing. |

> Quirk: if you *do* include `,<time>`, the server truncates its copy of the buffer at
> the comma **before** echoing it, but still reports the *original* `length`
> (`rfbserver.c:1912-1916`, `:1945`) ⇒ the accept header’s payload comes back with the
> comma byte replaced by `NUL`. Send the bare path and this cannot happen.

**(2) Accept / reject header** (server→client) — `rfbserver.c:1945`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 9 (rfbFileAcceptHeader) / 0 / 0` |
| size | `0` = accepted; **`0xFFFFFFFF` = rejected** |
| length + data | echo of the offer payload (as modified above), same `length` |

The server accepts whenever `open(path, O_CREAT|O_WRONLY|O_TRUNC, 0744)` succeeds
(`rfbserver.c:1939`) — i.e. rejection happens only on filesystem errors. No `sizeH`
trailer on this message.

**(3) Data packets**, N × (client→server) — decoded `rfbserver.c:1958-1989`:

| Field | Value |
|---|---|
| type / contentType / contentParam / pad | `7 / 5 (rfbFilePacket) / 0 / 0` |
| size | `0` = raw, `1` = zlib — `if (size==0) write(...) else uncompress(...)` (`rfbserver.c:1965-1975`) |
| length + data | ≤ 8192 raw bytes (compressed payload must decompress to ≤ 8192 — server buffer is `compBuff[sz_rfbBlockSize]`, `rfbserver.c:1734-1735`) |

**(4) End of file** (client→server): `7 / 6 / 0 / 0, size=0, length=0` — server closes
the fd (`rfbserver.c:1991-2000`) and **sends no acknowledgement**. Verify success
out-of-band if you need certainty.

**Errors:** if a server-side `write()` fails mid-upload, the server silently drops the fd
and then *ignores* all further packets until EOF (`rfbserver.c:1981-1988`,
`if (cl->fileTransfer.fd!=-1)` guard at `:1963`) — the client gets **no error
indication** (**protocol limitation, by this implementation**).

**Abort:** `7 / 7 / 0 / 0, size=0, length=0` — server closes fd, no reply
(`rfbserver.c:2002-2012`). ⚠ If no transfer is active (`fd == -1`) this message instead
enters the permission-handshake branch (§2d) — a reply of `rfbFileTransferAccess` or
`rfbAbortFileTransfer` may come back.

**Concurrency:** the server keeps a **single** `cl->fileTransfer` slot (one fd, one
direction: `rfbserver.c:1861-1864`, `:1952-1955`). Never pipeline two transfers; wait for
EOF/Abort (download) or send EOF (upload) before the next offer/request.

### 2d. Permission / authorization handshake (`rfbFileTransferAccess`)

Server configuration (why a handshake can be moot):

- `screen->permitFileTransfer` defaults to **FALSE** — `src\libvncserver\main.c:1037`
  (`screen->permitFileTransfer = FALSE;`), enabled by `-permitfiletransfer`
  (`src\libvncserver\cargs.c:118-119`) or `screen->permitFileTransfer = TRUE`
  (`include\rfb\rfb.h:346`).
- Optional per-client callback `getFileTransferPermission` (`rfb.h:137`, `rfb.h:308`).
- **Every** file-transfer entry point is gated by this macro — `rfbserver.c:1304-1311`:

```c
#define FILEXFER_ALLOWED_OR_CLOSE_AND_RETURN(msg, cl, ret) \
	if ((cl->screen->getFileTransferPermission != NULL \
	    && cl->screen->getFileTransferPermission(cl) != TRUE) \
	    || cl->screen->permitFileTransfer != TRUE) { \
		rfbLog("%sUltra File Transfer is disabled, dropping client: %s\n", msg, cl->host); \
		rfbCloseClient(cl); \
		return ret; \
	}
```

> **Key fact: against a default-configured LibVNCServer (permitFileTransfer = FALSE), the
> very first type-7 message the client sends gets the TCP connection closed
> (“Ultra File Transfer is disabled, dropping client”). There is no polite denial.**
> The macro guards `rfbProcessFileTransfer` itself (`rfbserver.c:1739`).

Handshake mechanics that *are* reachable (`rfbserver.c:2002-2053`, inside the
`rfbAbortFileTransfer` case, only when **no** transfer fd is open):

| Client sends | Server replies | Evidence |
|---|---|---|
| `7 / 7 (Abort) / contentParam == 0 / 0, length=0` | `7 / 7 / 0 / 0, size=0xFFFFFFFF, length=0` — *always denied by design* (“Old method … (<=RC18) … (Deny it)”, `rfbserver.c:2020-2022`) | `rfbserver.c:2018-2023` |
| `7 / 7 (Abort) / contentParam != 0 (e.g. 1) / 0, length=0` | `7 / 14 (rfbFileTransferAccess) / 0 / 0, size=1` (grant) — or `size=0xFFFFFFFF` (deny) | `rfbserver.c:2024-2051` |
| `7 / 14 (rfbFileTransferAccess) / …` (client→server) | **nothing** — only `rfbLog("…rfbFileTransferAccess\n")`, payload never read ⇒ must send `length=0` | `rfbserver.c:1799-1801` |

The comment at `rfbserver.c:2015-2017` identifies the probe:
`/* We use this message for FileTransfer rights (<=RC18 versions) * The client asks for
FileTransfer permission */`.

**Is it needed?** Against this server: *not strictly.* By the time the abort handler runs,
the macro has already verified permission, so the `size=0xFFFFFFFF` deny branches
(`rfbserver.c:2034-2035`, `:2046-2048`) are **unreachable dead code** — a surviving
connection always yields `rfbFileTransferAccess size=1`. A conservative client may probe
once with `contentParam=1` and treat:

- reply `size=1` → FT enabled, proceed;
- connection closed → FT disabled on the server (do not retry);
- **never** probe with `contentParam=0` (guaranteed fake-deny).

Whether non-LibVNCServer servers implement a real grant/deny handshake on
`rfbFileTransferAccess` is **AMBIGUOUS/UNVERIFIED** from this tree (no foreign source
present).

### 2e. Commands (delete / mkdir / rename) — brief

Request: `7 / 10 (rfbCommand) / <param> / 0, size=0, length+data = path`.
The payload is always read (`rfbserver.c:2060`). Handled params
(`rfbserver.c:2061-2107`):

| Param | Data | Reply (`rfbCommandReturn`, type 7, contentType 11) |
|---|---|---|
| `rfbCDirCreate = 1` | path | `contentParam = rfbADirCreate (4)`, `size = 0` ok / `0xFFFFFFFF` fail, payload = echo (`rfbserver.c:2062-2071`) |
| `rfbCFileDelete = 4` | path (rmdir/unlink auto-selected via `stat`, `rfbserver.c:2075-2081`) | `contentParam = rfbAFileDelete (7)`, size = 0/`0xFFFFFFFF`, echo (`:2072-2085`) |
| `rfbCFileRename = 5` | `"old*new"` — `'*'` is the separator (`rfbserver.c:2087`), restored before the echo (`:2101`) | `contentParam = rfbAFileRename (8)`, size = 0/`0xFFFFFFFF`, echo = `"old*new"` (`:2086-2105`) |

`rfbCDirDelete = 2`, `rfbCFileCreate = 3`, `rfbCDirRename = 6` are **not implemented**:
payload is consumed but **no reply is ever sent** (the switch has only 3 cases) — sending
them will hang a synchronous caller. `rfbRErrorUnknownCmd`/`rfbRErrorCmd`
(`rfbproto.h:1085-1086`) are never emitted as `contentParam`s; failures use
`size = 0xFFFFFFFF` (which equals `rfbRErrorCmd`).

### 2.6 Payload-consumption table (critical for stream integrity)

The server reads the message header (12 bytes) up-front (`rfbserver.c:2789-2796`) and
*then* decides whether to read `length` bytes. **Client→server messages must have
`length = 0` for every row marked “no payload read”, or the stream desynchronises** (the
server will interpret your payload bytes as message headers). Unknown `contentType`
values are swallowed silently with **no** payload read and **no** reply
(`rfbserver.c:1745-2110` has no default case) — connection stays up, stream corrupts.

| contentType (c→s) | payload read? | reply |
|---|---|---|
| 1 `rfbDirContentRequest` + param 1 | yes (`:1779`) | listing (§2a) |
| 1 + param 2 (drives) | **no** (`:1748-1774`) | 1 message |
| 1 + param 3/4 (recursive) | **no** | **none** |
| 3 `rfbFileTransferRequest` | yes (`:1812`) | FileHeader (+sizeH) |
| 4 `rfbFileHeader` (ack) | **no** (`:1879-1894`) | starts packet stream |
| 5 `rfbFilePacket` | yes (`:1962`) | none |
| 6 `rfbEndOfFile` | **no** (`:1991-2000`) | none |
| 7 `rfbAbortFileTransfer` | **no** (`:2002-2053`) | none if fd open; §2d reply if idle |
| 8 `rfbFileTransferOffer` | yes (`:1909`) + 4-byte sizeH (`:1923`) | FileAcceptHeader |
| 9 `rfbFileAcceptHeader` | **no** (`:1789-1791`, log only) | — (server→client type anyway) |
| 10 `rfbCommand` | yes (`:2060`) | CommandReturn for params 1/4/5 only |
| 11 `rfbCommandReturn` | **no** (`:1792-1794`) | — |
| 12 `rfbFileChecksums` | **no** (`:1795-1798`, log only) | — (**do not send**, §3.3) |
| 14 `rfbFileTransferAccess` | **no** (`:1799-1801`) | — |
| unknown (e.g. 13) | **no** | none, stream desyncs if `length>0` |

Additionally: `length > INT_MAX` ⇒ server logs an error and **closes the connection**
(`rfbserver.c:1594-1598`). Server→client directions not consumed by *our* client are our
own responsibility to read exactly (`length` + optional `sizeH`, §1.4).

---

## 3. Chunk size, compression, CRC/checksums

### 3.1 Chunk size

`#define sz_rfbBlockSize 8192  /* Size of a File Transfer packet (before compression) */`
— `rfbproto.h:1088`. The server reads at most 8192 raw file bytes per packet
(`rfbserver.c:1669`), upload chunks must decompress to ≤ 8192 (server output buffer
`compBuff[sz_rfbBlockSize]`, `rfbserver.c:1734-1735`). The last packet of a file is
shorter; EOF is signalled by `rfbEndOfFile`, not by a short read.

### 3.2 Compression (zlib)

- **Algorithm:** zlib `compress()` / `uncompress()` (RFC 1950 zlib-wrapped stream), one
  *independent* stream per packet — server side: `compress(compBuf, &nMaxCompSize, readBuf,
  bytesRead)` (`rfbserver.c:1702`) and `uncompress(compBuff,&nRawBytes,buffer,length)`
  (`rfbserver.c:1971`). Not raw deflate, not a connection-wide stream.
- **Direction flag:** `rfbFilePacket.size == 1` means “payload is zlib”, `0` means raw —
  both directions (`rfbserver.c:1697-1713` send, `rfbserver.c:1965-1979` receive).
- **Download (server→client):** compression is *offered* by setting `size=1` in the
  `rfbFileTransferRequest` (`rfbserver.c:1845-1846`). Even when offered, the server
  falls back to a raw packet if zlib is unavailable at build time
  (`#else … rfbSendFileTransferMessage(cl, rfbFilePacket, 0, 0, …)`, `rfbserver.c:1711-1713`)
  or if compression does not shrink the chunk: `if ((nRetC==0) && (nMaxCompSize<bytesRead))`
  (`rfbserver.c:1707-1710`). ⇒ **Always be prepared to receive both `size=0` and `size=1`
  packets.** Only set `size=1` in the request if you can decompress.
- **Upload (client→server):** the server decompresses whenever `size==1`
  (`rfbserver.c:1967-1975`), but if the server was built without zlib it will
  `write()` the compressed bytes **verbatim** (`#else` branch, `rfbserver.c:1976-1979`) —
  silent corruption. There is **no capability signal** for “server accepts compressed
  uploads” in this protocol. **Recommendation: send uploads raw (`size=0`) by default.**
  Inferring zlib support from the server’s advertised encodings (e.g.
  `rfbEncodingSupportedEncodings`) is a heuristic — **UNVERIFIED**.
- If `uncompress()` fails server-side, the server closes the file and stops storing data
  without telling the client (`rfbserver.c:1974-1987`).

### 3.3 CRC / checksums / “v2”

- **There is no per-packet CRC or checksum in this protocol, in either direction, in this
  tree.** (Tree-wide grep for CRC/checksum in the FT code finds only `rfbproto.h:1054`
  and the stub below.)
- `rfbFileChecksums = 12 /* The zipped checksums of the destination file (Delta Transfer)
  */` — `rfbproto.h:1054`. The server’s handler is a stub that only logs
  (`rfbserver.c:1795-1798`) and the offer path has `/* TODO: Delta Transfer */`
  (`rfbserver.c:1937`). **Delta transfer is not implemented; sending contentType 12 with
  any payload desyncs this server** (no payload read — §2.6).
- `rfbFileTransferVersion 2` (`rfbproto.h:1039`, “v1 is the old FT version (<= 1.0.0 RC18
  versions)”) is **never transmitted or negotiated** anywhere in the tree (grep: only the
  definition). The only observable “v2” wire feature is the 4-byte `sizeH` trailer (§1.4)
  and the `rfbFileTransferAccess`-based permission flow (§2d).
- Integrity beyond that is left to the application (verify size/mtime after transfer).

---

## 4. What the client must advertise / enable; variant differences

### 4.1 For the type-7 (UltraVNC-style) protocol against LibVNCServer-based servers

**Nothing needs to be advertised.** The server dispatches `case rfbFileTransfer:` purely
on the message type byte — `rfbserver.c:2789-2801` — with no capability check (contrast
the `default:` branch at `rfbserver.c:3157-3177`, which closes the connection on unknown
types). The only “gate” is server-side configuration (`permitFileTransfer` /
`getFileTransferPermission`), §2d.

Capability machinery that exists but is **advisory only**:

- The server can *tell* the client its supported-message bitmap if the client requests
  pseudo-encoding `rfbEncodingSupportedMessages = 0xFFFE0001` (`rfbproto.h:536`; server
  reaction `rfbserver.c:2510-2515`, sender `rfbSendSupportedMessages`, `rfbserver.c:1036-1096`).
  libvncclient always requests it — `src\libvncclient\rfbclient.c:1434-1440`
  (`encs[se->nEncodings++] = rfbClientSwap32IfLE(rfbEncodingSupportedMessages);`) — and
  reads the reply into `client->supportedMessages` (`rfbclient.c:2192-2215`, comment at
  `:2198`: `/* currently ignored by this library */`).
- **Quirk / likely bug:** the server’s bitmap sets `client2server` bit 7
  (`rfbSetBit(msgs.client2server, rfbFileTransfer);`, `rfbserver.c:1066`) but **never sets
  the `server2client` bit for `rfbFileTransfer`** (the s2c list at `rfbserver.c:1073-1079`
  omits it), although it does send type-7 messages. **Do not use the s2c bitmap to decide
  whether type-7 messages can arrive.**
- The client-side bitmap `client->supportedMessages` is populated locally by
  `DefaultSupportedMessages*` and consulted only via `SupportsClient2Server` /
  `SupportsServer2Client` (`rfbclient.c:188-198`) — **no callers exist in this tree**, so
  libvncclient does not gate sending. Defaults: plain `DefaultSupportedMessages` does
  **not** include type 7 (`rfbclient.c:226-245`); it is added by:
  - `DefaultSupportedMessagesUltraVNC` — `SetClient2Server(client, rfbFileTransfer);` and
    `SetServer2Client(client, rfbFileTransfer);` (`rfbclient.c:247-262`), triggered when
    the server version is 3.4/3.6 or 3.14/3.16 (“UltraVNC server detected…”,`
    rfbclient.c:998-1012`) or when security type `rfbUltra (17)` is offered
    (`rfbclient.c:492-496`);
  - `DefaultSupportedMessagesTightVNC` — also sets both type-7 bits
    (`rfbclient.c:266-276`), triggered by version 3.5 (`rfbclient.c:1014-1019`) or
    security type `rfbTight (16)` (`rfbclient.c:497-500`).

  Because these bitmaps are never sent to the server, they do not affect whether *our*
  type-7 messages are accepted. (They do tell you what the *server flavor* was detected
  as — useful heuristics for expected behaviour.)

**Receiving type 7 with libvncclient — you must provide an extension.** There is **no
`case rfbFileTransfer` in `HandleRFBServerMessage`** (grep of
`src\libvncclient\rfbclient.c` finds type-7 only in the `Set*` bitmap functions at lines
251/260/269/274). The dispatch path:

- `rfbclient.c:2059-2062` — `if (!ReadFromRFBServer(client, (char *)&msg, 1)) return FALSE;`
  `switch (msg.type) {` ⇒ **only 1 byte has been consumed** when the default branch runs;
- `rfbclient.c:2724-2739` —
  ```c
    default:
      { rfbBool handled = FALSE; rfbClientProtocolExtension* e;
        for(e = rfbClientExtensions; !handled && e; e = e->next)
          if(e->handleMessage && e->handleMessage(client, &msg))
            handled = TRUE;
        if(!handled) {
          char buffer[256];
          rfbClientLog("Unknown message type %d from VNC server\n",msg.type);
          ReadFromRFBServer(client, buffer, 256);
          return FALSE;
        } }
  ```
  ⇒ the extension **must read the remaining 11 header bytes and the payload itself**;
  ⇒ if **no** extension claims the type: log + a best-effort 256-byte drain +
  `return FALSE`. The drain does not resynchronise (a type-7 payload is variable
  length), and `FALSE` is treated as fatal by every example client loop
  (e.g. `examples\client\ppmtest.c:109` `if(!HandleRFBServerMessage(client))`,
  likewise `SDLvncviewer.c:644`, `gtkvncviewer.c:669`, `qt5client.cpp:171`).
  **Net effect: without a registered extension, the first server→client file-transfer
  message aborts the client’s message loop.**
- `rfbClientRegisterExtension` (`rfbclient.c:105-109`) merely prepends to a global list
  (`e->next = rfbClientExtensions; rfbClientExtensions = e;`) ⇒ **LIFO order**; call it
  before `rfbInitClient`. Struct: `include\rfb\rfbclient.h:743-755`
  (`encodings`, `handleEncoding`, `handleMessage`, `next`, `securityTypes`,
  `handleAuthentication`).
- Canonical minimal pattern (working example) — `examples\client\backchannel.c:42-77`:
  check `message->type`, then `ReadFromRFBServer(client, ((char*)&msg)+1, sizeof(msg)-1)`
  (`:53`), swap (`:55`), read payload (`:57`), return `TRUE`. Register before init
  (`backchannel.c:85-87`).

Sending: use `WriteToRFBServer(rfbClient*, const char*, unsigned int)`
(`rfbclient.h:764`; implementation `src\libvncclient\sockets.c:250`). **No locking is done
inside it** — build header+payload(+sizeH) into one buffer and issue a single call, or
serialize writers yourself (**UNVERIFIED** thread-safety; the server-side equivalent does
lock via `sendMutex`, `rfbserver.c:1329`).

### 4.2 TightVNC 1.x vs TightVNC 2.x vs UltraVNC

**Explicit tree statement about variants** — `examples\server\filetransfer.c:1-5`:

```c
/**
 * @example filetransfer.c
 * Demonstrates a server capable of TightVNC-1.3.x file transfer.
 * NB That TightVNC-2.x uses a different, incompatible file transfer protocol.
 */
```

- **UltraVNC** — the type-7 protocol *is* the UltraVNC-style one: “Modif sf@2002”
  (`rfbproto.h:1021`), `/* Modif sf@2002 - actually bidirectionnal */` above
  `#define rfbFileTransfer 7` (`rfbproto.h:421-422`), the pad comment (`rfbproto.h:1030`),
  “UltraVNC uses Windows Structures” (`rfbserver.c:1354-1355`), and client detection of
  UltraVNC servers (§4.1). **Original UltraVNC sources are not in this tree**, so any
  deviation of real UltraVNC from LibVNCServer’s implementation (notably `sizeH`
  placement, §1.4, and whether entries use `rfbAFile` for files, §2a) is **UNVERIFIED**.
- **TightVNC 1.x** — two things are called “TightVNC file transfer” in this tree:
  1. *LibVNCServer’s type-7 support being enabled when a TightVNC server is detected* —
     `DefaultSupportedMessagesTightVNC` (`rfbclient.c:266-276`) implies TightVNC (1.x)
     viewers/servers are believed to speak type 7 too. `NEWS.md:414-417`: “he
     contributed TightVNC file transfer protocol support to LibVNCServer!” (2005);
     `NEWS.md:188` “Made the TightVNC-style file transfer more stable.”;
     `NEWS.md:323-324` “Fix for tight security type for RFB 3.8 in TightVNC file
     transfer”.
  2. *The separate TightVNC-1.3.x extension* in
     `src\libvncserver\tightvnc-filetransfer\` — that is what
     `examples\server\filetransfer.c` demonstrates (`rfbRegisterTightVNCFileTransferExtension()`,
     `:11`). Summary (server side is the only implementation present):
     - Message types (negotiated, not fixed): client→server `rfbFileListRequest=130`,
       `rfbFileDownloadRequest=131`, `rfbFileUploadRequest=132`, `rfbFileUploadData=133`,
       `rfbFileDownloadCancel=134`, `rfbFileUploadFailed=135`,
       `rfbFileCreateDirRequest=136`; server→client `rfbFileListData=130`,
       `rfbFileDownloadData=131`, `rfbFileUploadCancel=132`,
       `rfbFileDownloadFailed=133` — `rfbtightproto.h:229-257`, structs at
       `:285-450`. All 16-bit fields big-endian (`Swap16IfLE`, e.g.
       `handlefiletransferrequest.c:317`, `:406`), 32-bit big-endian
       (`filetransfermsg.c:388-389`).
     - Requires negotiating security type `rfbSecTypeTight = 16` (`rfbtightproto.h:37`);
       the extension is *only* enabled for such connections — it has `newClient == NULL`
       so it is not auto-enabled (`rfbserver.c:569-576` + `rfb.h:171-173`), it is enabled
       in `rfbHandleSecTypeTight` (`rfbtightserver.c:497-519`, enable at `:515`), and its
       caps are sent after ServerInit via the extension `init` hook
       (`rfbserver.c:924-931` → `rfbTightExtensionInit`, `rfbtightserver.c:364-371`).
     - `rfbInteractionCapsMsg` (`rfbtightproto.h:218-227`) then lists the FT codes only
       `if((IsFileTransferEnabled() == TRUE) && ( cl->viewOnly == FALSE))`
       (`rfbtightserver.c:288-318`; note `N_CMSG_CAPS=6` at `:268` **omits
       `rfbFileCreateDirRequest` (136)** even though it is handled, `:425-427`).
     - Listing reply: `rfbFileListData` = header + `numFiles × {uint32 size, uint32 mtime}`
       (both BE) + NUL-separated names; directories carry `size = -1`, `mtime = 0`
       (`filetransfermsg.c:357-400`, `filelistinfo.h:45-48`); error flag `flags |= 0x80`
       (`filetransfermsg.c:344-353`). Download: 8192-byte chunks
       (`#define SZ_RFBBLOCKSIZE 8192`, `filetransfermsg.c:71`) inside
       `rfbFileDownloadData`; EOF marker = `realSize = compressedSize = 0` followed by a
       `uint32 mtime` (`filetransfermsg.c:545-576`, receive side
       `handlefiletransferrequest.c:832-845` for the upload mirror); errors are
       `rfbFileDownloadFailed`/`rfbFileUploadCancel` with a `reason` string
       (`filetransfermsg.c:515-542`, `:702-729`). **No compression in either direction**
       — server always sends `compressLevel = 0` (`filetransfermsg.c:599`) and rejects
       compressed uploads with “Server does not support data compression on upload”
       (`filetransfermsg.c:651-657`, triggered `handlefiletransferrequest.c:867-888`).
       Sending any type-130+ message while FT is disabled/view-only ⇒ connection closed
       (`rfbtightserver.c:382-385`).
     - (Task note: there is **no `handlefilelistrequest.c`** in the tree; list handling
       lives in `handlefiletransferrequest.c:289-359` and `filetransfermsg.c:88-400`.)
- **TightVNC 2.x** — **the tree explicitly says its file transfer is a *different,
  incompatible* protocol (`examples\server\filetransfer.c:4`) and contains *no* TightVNC
  2.x sources.** Its wire format is therefore **UNVERIFIED / out of scope of this
  document** — do not assume type 7 or types 130+ against TightVNC 2.x.

---

## 5. Implementation checklist (client)

**Setup**

1. Construct the client with `rfbGetClient()` (or set `client->endianTest = 1`
   yourself), see §1.2.
2. Register a `rfbClientProtocolExtension` with `rfbClientRegisterExtension()` **before**
   `rfbInitClient` (`rfbclient.c:105-109`; example `backchannel.c:85-87`). Its
   `handleMessage` must: return `FALSE` immediately for `message->type != 7`; otherwise
   read 11 more bytes into an `rfbFileTransferMsg` via `ReadFromRFBServer`, apply
   `rfbClientSwap32IfLE` to `size`/`length`, read `length` payload bytes, and — only for
   `contentType == 4` with `size != 0xFFFFFFFF` — read the trailing 4-byte `sizeH`.
   Return `TRUE` when the type was consumed (even on read failure — returning `FALSE`
   kills the caller’s loop).
3. Provide `encodings` in the extension only if you have a real pseudo-encoding; the
   type-7 protocol has **none**. libvncclient already advertises
   `rfbEncodingSupportedMessages` (`rfbclient.c:1434-1440`) — optionally read the server
   bitmap, but ignore its `server2client` type-7 bit (§4.1).

**Before any type-7 write**

4. Be aware the server may be `permitFileTransfer = FALSE` (the default) and will then
   **close the connection on your first type-7 message** (§2d). Treat an immediate
   disconnect after the first FT message as “server has FT disabled”, not as a protocol
   bug. Optional probe: `7,7,0,1 / size=0,length=0` → expect `7,14,…,size=1`
   (`rfbserver.c:2024-2051`); never probe with `contentParam=0`.
5. Serialize writes (single `WriteToRFBServer` call per message, §4.1) and keep `length`
   exactly equal to the payload you append — see the payload-consumption table §2.6
   (sending payload where the server reads none corrupts the stream *without* a
   disconnect; the server only rejects `length > INT_MAX` by closing,
   `rfbserver.c:1594-1598`).

**Sequences (exact order of writes → expected reads)** — §2a–§2e:

6. Listing: write request `7,1,1,0,0,len,path` → read
   `[7,2,1,…,len,path]`, then N × `[7,2,1,…,L,FIND_DATA (LE fields)]`, then
   `[7,2,0,0,0,0]`; a first reply with `L==0` means failed/empty (no terminator will
   come). Drives: `7,1,2,0,0,0` → `[7,2,3,…,5,"C:l\0\0"]`.
7. Download: write `7,3,0,0,(1|0),len,path` → read `[7,4,0,0,size,path+time]` (+4-byte
   trailer iff `size != 0xFFFFFFFF`) → write ack `7,4,0,0,filesize,0,0` (or `size`
   `0xFFFFFFFF` to refuse) → read `7,5` packets (`size` 0/1 = raw/zlib) until
   `[7,6,0,0,0,0]` (EOF) or `[7,7,0,0,0,0]` (abort). No ACKs are needed or expected;
   chunks are pumped by the server loop (`main.c:571,587`; non-blocking select inside
   `rfbSendFileTransferChunk`, `rfbserver.c:1652-1667`).
8. Upload: write offer `7,8,0,0,filesize,len,path` **+ 4 sizeH bytes** → read
   `[7,9,0,0,0|0xFFFFFFFF,…]` (reject ⇒ abort your side; no further bytes come) → write
   `7,5` packets (`size=0` raw recommended, ≤8192 raw each) → write `7,6,0,0,0,0` →
   **expect silence** (no ack; server write errors are silent, §2c). Verify the file
   out-of-band if correctness matters.
9. Abort any transfer with `7,7,0,0,0,0`; if idle, expect either silence, a deny
   `7,7,…,0xFFFFFFFF` (if you used param 0), or `7,14,…,size=1` (if you used param ≠ 0).
10. Commands: `7,10,{1|4|5},0,0,len,data` → `[7,11,{4|7|8},0,size∈{0,0xFFFFFFFF},len,data]`.
    Never use params 2/3/6 (no reply will ever come).

**Robustness / timeouts**

11. Tolerate interleaving: framebuffer updates, bell, cut-text etc. continue during
    transfers; they flow through the normal `HandleRFBServerMessage` switch — your FT
    state machine must not assume FT messages arrive contiguously.
12. One transfer at a time per connection (single server-side fd, §2c).
13. Apply application-level timeouts: (a) time-to-first-response after a request, (b)
    stall timeout during download (server pushes chunks continuously while the socket is
    writable; its select loop ticks at up to 60 s, `main.c:560,569-571`, so a multi-minute
    gap means trouble). libvncclient offers `client->readTimeout` for half-open detection
    (`rfbclient.h:481-483`, default 0 = disabled, `rfbclient.h:89`) and
    `connectTimeout` (`:478-479`).
14. Cap inbound `length` before allocating (precedent: ServerCutText’s 1 MB guard,
    `rfbclient.c:2606-2609`); sane FT bounds: ≤ 64 KiB for path/header payloads,
    ≤ 8192+1024 for packet payloads (`compBuf[sz_rfbBlockSize + 1024]`, `rfbserver.c:1629`).
15. Treat `HandleRFBServerMessage() == FALSE` as fatal (your loop will exit, §4.1);
    therefore your extension must never return `FALSE` for a type-7 message it recognised.
16. Do not send: `rfbFileChecksums` (12), `rfbFileAcceptHeader`/`rfbCommandReturn`/
    `rfbDirPacket` (server→client types), `rfbFileTransferAccess` with payload, recursive
    dir requests, or any unknown `contentType` with payload (§2.6).

---

## Appendix — known gaps / AMBIGUITY register

| Topic | Status |
|---|---|
| `sizeH` placement vs. original UltraVNC (header vs. payload trailer) | **AMBIGUOUS** — tree only proves the payload-trailer layout for LibVNCServer (§1.4) |
| `sizeH` byte order on server→client writes (written raw, always 0) | indistinguishable from BE; use BE (§1.4) |
| Directory entry exact length (46+strlen vs 44+strlen, ABI padding) | **UNVERIFIED** (derived from ABI rules, not measured); parse robustly (§1.5) |
| Whether other servers send `rfbAFile` (2) for file entries | **AMBIGUOUS** — this tree always sends `rfbADirectory` (§2a) |
| `"[ dir ]"`/`"[ file ]"` prefixes, zipped-directory transfer (`!UVNCDIR-`) | defined only, **unused in tree** (§1.5) |
| Real `rfbFileTransferAccess` deny flows | deny branches unreachable on LibVNCServer (§2d); foreign behaviour **UNVERIFIED** |
| Compressed-upload support detection | no capability signal; recommend raw (§3.2) |
| Delta transfer / checksums (contentType 12) | unimplemented stub (§3.3) |
| >4 GiB files | `size` is 32-bit; server always sends `sizeH = 0` (`rfbserver.c:1866`); completion must use `rfbEndOfFile` (§2b) |
| TightVNC 2.x file transfer protocol | **not in tree**; explicitly incompatible (`examples\server\filetransfer.c:4`); **UNVERIFIED** |
| `WriteToRFBServer` thread-safety (client) | no locking found (**UNVERIFIED**); serialize yourself (§4.1) |
| `endianTest` on big-endian hosts / non-`rfbGetClient` construction | see §1.2 |
