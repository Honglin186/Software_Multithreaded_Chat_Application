# ELEC50014 Assignment 2 — Multithreaded Chat Application

This project uses UDP and POSIX threads to build a chat server in C. As of 19 November 2026, the server has a listener thread, a worker for each incoming request, and a shared list of clients. Registration with `conn$` is in progress. The server can add a client to the list. Sending confirmation back to that client is the next step. [Assignment brief](https://sarimbaig01.github.io/elec50014/Docs/Assignment2.html)

## Build and run

From the project directory, compile both programs and start the server in one terminal.

```sh
cc -Wall -pthread chat_server.c -o chat_server
cc -Wall chat_client.c -o chat_client
./chat_server
```

Open a second terminal in the same directory and run `./chat_client`. The current test client binds to UDP port `10000` and sends the fixed request `conn$ Alice` to `127.0.0.1:12000`. It then waits for a reply. At this milestone, the greeting is printed in the server terminal and the client continues waiting. Press Ctrl-C in each terminal to stop the programs.

## Implementation

### 1. Verify the UDP exchange

The starter client sent `Dummy Request`. The server used the sender address returned by the receive call to send a reply. The client displayed `Hi, the server has received: Dummy Request`, and the server returned to listening. This established a working baseline before threading was introduced. [Client output](images/initial1.png) · [Server output](images/initial2.png)

### 2. Move request handling into worker threads

The main thread opens the server socket and starts a listener. For each incoming datagram, the listener allocates a separate `Request` containing the message, the sender's address, and the socket descriptor. It passes that object to a detached worker and can then receive the next message. Each worker owns its request until it frees it, so a later datagram cannot overwrite the address needed for an earlier reply.

In a loopback test of this version, two clients sent different messages concurrently and each received its own echo. [Worker and server output](images/initial_worker.png)

### 3. Register a client with `conn$`

The worker splits the request at the first `$` and dispatches `conn` to `connect_client`. This function stores the sender's IP address, port, and chat name in a linked-list node. It holds a write lock while inserting the node because several workers can register clients at the same time.

The milestone screenshot shows Alice's greeting in the server terminal. In a separate check of the version shown, three registration requests for Alice created three nodes and sent zero UDP replies. The current parser also measures the name from a different position than it copies from. Completing registration means copying a trimmed name of 1–31 bytes with its terminator, checking the name and endpoint under the insertion lock, and replying to the sender with the result. [Registration screenshot](images/milestone3.png)

## Work distribution and next steps

| Team member | Work so far and agreed responsibilities |
|---|---|
| Honglin | Implemented the server listener, per-request workers, and initial registration handler. Owns the remaining server commands, connection history, and inactive-client removal. |
| Nicky | Assigned the interactive client, automatic ping replies, and multi-client integration tests. These deliverables are pending. |
