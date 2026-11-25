# ELEC50014 Assignment 2 — Multithreaded Chat Application

This C chat application uses IPv4 UDP and POSIX threads. The server in team commit `fba9b8d` builds and supports registration, broadcast, private messaging, rename, and disconnect for registered users. Honglin's 25 November development copy adds moderation, connection history, and an inactivity monitor. Its clean build currently fails because `remove_activity` is called before its declaration. The results below identify which features have been exercised end to end and which were checked in isolation. [Assignment brief](https://sarimbaig01.github.io/elec50014/Docs/Assignment2.html)

## Build and run

From the repository directory, compile the checked-in version and start the server.

```sh
cc -Wall -pthread chat_server.c -o chat_server
cc -Wall chat_client.c -o chat_client
./chat_server
```

In a second terminal in the same directory, run `./chat_client`. The test client sends `conn$ Alice` from UDP port `10000` to `127.0.0.1:12000`, prints `server_response: you have successfully connected to the chat`, and exits. Use Ctrl-C in the server terminal to stop it. The client currently sends this one fixed request. The multi-client tests used separate UDP endpoints.

## Implementation

### 1. Verify the UDP exchange

The starter client sent `Dummy Request`. The server used the sender address returned by the receive call to send a reply. The client displayed `Hi, the server has received: Dummy Request`, and the server returned to listening. This established a working baseline before threading was introduced. [Client output](images/initial1.png) · [Server output](images/initial2.png)

### 2. Move request handling into worker threads

The main thread opens the server socket and starts a listener. For each incoming datagram, the listener allocates a separate `Request` containing the message, the sender's address, and the socket descriptor. It passes that object to a detached worker and can then receive the next message. Each worker owns its request until it frees it, so a later datagram cannot overwrite the address needed for an earlier reply.

In a loopback test of this version, two clients sent different messages concurrently and each received its own echo. [Worker and server output](images/initial_worker.png)

### 3. Register a client with `conn$`

The worker splits the request at the first `$` and dispatches `conn` to `connect_client`. This function stores the sender's IP address, port, and chat name in a linked-list node. It holds a write lock while inserting the node because several workers can register clients at the same time.

The first registration milestone produced a greeting in the server terminal. The development version now replies to the UDP endpoint that requested registration. It skips whitespace after the first `$` before storing the name. [Registration screenshot from the earlier version](images/milestone3.png)

### 4. Route messages and update sessions

The server identifies a sender by its IP address and port. For a broadcast, a worker takes a read lock and walks the client list, sending once to each address. For a private message, it splits off the recipient's name at the first space and sends the remaining text to that client. Rename and disconnect take the write lock to update the list. Disconnect unlinks the matching node and releases the lock before sending the goodbye.

The following results come from a loopback test on 24 November using the development server (source SHA-256 prefix `93ce514eb8a8`). Alice, Bob, and Eve used distinct UDP ports. The test exercised the server's listener and workers and checked the receiving sockets after each command.

| Action | Observed result |
|---|---|
| Each client registered with `conn$` | Alice, Bob, and Eve each received a confirmation at their own endpoint. |
| Alice sent `say$ Hello everyone` | All three received `Alice: Hello everyone` once, including Alice. |
| Alice sent `sayto$ Bob Meet at the library` | Only Bob received `Alice: Meet at the library`. |
| Alice sent `rename$ Alicia` | Alice received confirmation. Eve could address a private message to Alicia, and Alicia's next broadcast showed the new name. |
| Bob sent `disconn$` | Bob received `Disconnected. Bye!`. Eve's next broadcast reached Eve and Alicia only. Follow-up broadcasts also succeeded after removing the tail and head of the list. |

### 5. Moderation, history, and inactivity

The 25 November development snapshot (`f025ab507577`) contains handlers for all eight user commands. Since that snapshot does not compile on its own, the reviewer used a temporary harness that supplied the missing function declaration, seeded registered clients, and captured outgoing messages. These checks exercised individual code paths without a running chat client.

| Feature | Design and observed result | Remaining work |
|---|---|---|
| `mute$` and `unmute$` | Each recipient keeps a linked list of muted session IDs. Alice continued to filter Bob's broadcasts after his rename, while Eve received them. Unmuting restored Alice's delivery. | Private delivery must check the recipient's mute list. Repeated mutes and client removal also need to clean up mute entries and prevent a new session from inheriting a reused ID. |
| Admin `kick$` | The handler checks source port `6666`. An ordinary endpoint received `No Permission.`. For an admin kick of a non-head client, the target received a removal notice and each remaining endpoint received the announcement. | Removing the list head invalidates the saved broadcast cursor. Begin traversal from the updated head and handle a missing target before using its address. |
| **Part 3. History on connection** | Public broadcasts enter a 15-slot circular buffer. With 17 numbered broadcasts and a private message, the stored history contained exactly broadcasts 3–17 in order. | Replay uses reader–writer lock functions on a mutex and leaves the partial-history loop counter uninitialised. Fix both and coordinate the history snapshot with registration so a concurrent broadcast is delivered once. |
| **Part 4. Remove inactive clients** | Two timestamp-ordered linked lists track active clients and outstanding pings using a monotonic clock. The idle threshold is 300 seconds and the reply window is 10 seconds. Two pre-aged test clients were pinged. Calling the refresh helper directly kept one active, and the monitor removed the silent one after the grace period. | The listener updates activity before first registration and compares the ping reply by pointer. Guard session lookups, recognise `ret-ping$` by its contents, and keep each client in exactly one activity list. The interactive client still needs automatic replies. |

### Next integration check

First restore a clean build. Then use real client endpoints to check that a newcomer receives exactly messages 3–17, that mute applies to private messages, that an admin can kick the most recently joined client, and that a quiet client replying to pings stays while a silent client is removed. Registration and rename also need unique-name checks, and handlers must handle missing clients safely.

## Work distribution

| Team member | Contribution and responsibilities |
|---|---|
| Honglin | Implemented the threaded server and command handlers. Added public history, the activity lists, and the ping monitor in the development copy. Owns the remaining server fixes and integration. |
| Nicky | Assigned the interactive client, automatic ping replies, and team multi-client integration tests. The checked-in client is the single-request test program. |

History and inactivity are core assignment requirements. The team will choose a stretch feature after core client/server integration.
