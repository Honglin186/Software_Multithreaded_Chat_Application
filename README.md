# ELEC50014 Assignment 2 — Multithreaded Chat Application

This C server uses UDP and POSIX threads to let clients register, exchange messages, change names, and leave the chat. On 24 November, those five commands passed a three-client test in Honglin's development copy. The team repository at `588bda7` contains the earlier registration milestone. [Assignment brief](https://sarimbaig01.github.io/elec50014/Docs/Assignment2.html)

## Build and run

From the directory containing the C sources, compile both programs and start the server.

```sh
cc -Wall -pthread chat_server.c -o chat_server
cc -Wall chat_client.c -o chat_client
./chat_server
```

In a second terminal in the same directory, run `./chat_client`. This single-request test client binds to port `10000` and sends `conn$ Alice` to `127.0.0.1:12000`. With the development server, the client prints `server_response: you have successfully connected to the chat` and exits. With the repository version at `588bda7`, the greeting appears in the server terminal and the client waits for a reply. Use Ctrl-C to stop a waiting process. The messaging tests below used three separate UDP endpoints.

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

**Known limitations.** The server accepted both a new registration and a rename using a name already in use. This makes private-message routing ambiguous. The private-message handler must also check that both clients exist, and rename must check that the sender is registered. Public and private sends currently hold the registry read lock during socket writes, which can delay membership changes.

## Work distribution and next steps

| Team member | Work so far and agreed responsibilities |
|---|---|
| Honglin | Implemented the threaded server and the five commands tested above. Owns server validation, the moderation commands, connection history, and inactive-client removal. |
| Nicky | Assigned the interactive client, automatic ping replies, and multi-client integration tests. These deliverables are pending. |

The next server steps are to enforce unique names, handle missing clients, and implement `mute$`, `unmute$`, and admin-only `kick$`. Connection history and inactivity monitoring are planned core features. Stretch-feature selection is deferred until core integration.
