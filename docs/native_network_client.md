# Native network client

The Windows player can connect its scene to a dedicated server over UDP. Both a
project run and a cooked, renamed shipping executable accept:

```text
NetworkSample.exe --network-config=../Sessions/session.json
```

Create the session file at runtime, outside the project's cooked assets:

```json
{
  "address": "127.0.0.1",
  "port": 27015,
  "clientId": "7",
  "key": "3131313131313131313131313131313131313131313131313131313131313131",
  "worldExtent": 1000,
  "reconnectAttempts": 3
}
```

The example key is sample data. Supply the session key issued for that client;
the server's key table must match it. A server requiring signed authorization
also needs the issued login token in the optional `token` string. Credentials
are supplied when launching the player and do not belong in a distributable
asset pack.
Keep the session file outside a sealed distribution so its recorded payload
remains unchanged.

`address` is a literal IPv4 address, and `port` is an integer from 1 to 65535.
`clientId` is a decimal **string**, preserving all 64 bits. `key` contains exactly
64 hexadecimal digits. `worldExtent` is the shared transform codec extent,
greater than zero and at most 10,000,000; use the same extent on the server.
`reconnectAttempts` is an integer from zero to ten. The settings file is limited
to 8192 bytes. Invalid settings fail before the player creates a window, without
printing the key or token.

The client attaches before the first script update. Scripts can declare RPCs in
`Ready`, then receive `OnNetConnected`, `OnNetDisconnected`, `OnNetSpawned`,
`OnNetDespawned`, and their declared RPC callbacks. Network events and transform
replication are pumped after each scene update. A lost connection uses the shared
client's bounded reconnect behavior; rejected authorization stops reconnects.

Replicated transforms are quantized over the configured world extent. Gameplay
can use the local entity passed to `OnNetSpawned` to drive a visual object. The
network package test demonstrates this with a rendered proxy and a real server
restart. The browser host continues to use its WebSocket session settings.

After building the native test and runtime targets, run:

```text
ctest --test-dir build/release -R "^(kb_net_script_tests|kb_game_native_network_package)$" --output-on-failure
```

The package workflow creates projects through the CLI, cooks the client, hides
its loose source, launches the shipping executable with a restricted PATH,
checks replication and RPC before and after server restart, and captures a
rendered frame from the final asset pack.
