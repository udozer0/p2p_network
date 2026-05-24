# p2p_network Architecture

`p2p_network` is a reusable networking layer for applications that need direct device-to-device communication.

The password manager use case is the main product shape: several devices owned by one user should discover each other,
connect, and exchange encrypted application payloads. The networking layer must not know what a password is and must not
receive plaintext secrets.

## Layers

```text
Application
  Password manager, sync policy, encryption, user confirmation.

Public API
  Stable C++ interface used by the application.

Session
  Peer identity, connection lifecycle, heartbeat, reconnect, message routing.

Discovery
  Manual invite, LAN discovery, signaling, future DHT/relay metadata.

Transport
  TCP direct today, later UDP/WebRTC/relay fallback.
```

## Design Rules

- Application data is opaque bytes to this project.
- Passwords or vault items must be encrypted before they enter this layer.
- Direct transport is preferred, but relay fallback is acceptable if payloads are end-to-end encrypted.
- Discovery must be pluggable: LAN, manual invite, signaling, and future mechanisms should feed the same peer model.
- The CLI binary is only a demo and diagnostics tool. Other projects should link the library target.

## Current State

- TCP direct transport.
- Text-line protocol for control messages: `HELLO`, `PEERS`, `PUBLIC`, `PING`, `PONG`.
- STUN public IP discovery with fallback servers.
- Optional explicit public TCP address using `P2P_PUBLIC_IP`.
- Reconnect for known outbound peers.

## Near-Term Roadmap

1. Introduce a stable `p2p::P2PNode` API.
2. Replace ad-hoc line messages with framed messages.
3. Add app payload delivery callbacks.
4. Add manual invite format.
5. Add LAN discovery.
6. Add signaling as a separate discovery provider.
7. Add relay/WebRTC transport options.
