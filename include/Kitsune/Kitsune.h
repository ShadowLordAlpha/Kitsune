#pragma once

// Umbrella header. Include the module you need once those headers exist.
// Planned homes, added when a behavior is actually lifted:
//
//   kitsune::archive   MPQ as 4.10 reads maps. A 5.0 Windows client is the
//                      reference if that parser is newer than 4.10.
//   kitsune::net       lockstep peers, ports, join. First interoperability
//                      surface with a live 4.10 Linux client.
//   kitsune::api       SC2API websocket host/guest behavior.
//   kitsune::sim       game simulation. Last, and only behind tests.
//
// In-memory layouts are ordinary C++23. Bytes on the wire, in an MPQ, and in
// a replay have to match 4.10. See docs/decompile.md.

#include "Kitsune/Api/CreateGame.h"
#include "Kitsune/Api/JoinGame.h"
#include "Kitsune/Api/Lobby.h"
#include "Kitsune/Api/MapJoin.h"
#include "Kitsune/Api/Protocol.h"
#include "Kitsune/Net/BaseConn.h"
#include "Kitsune/Net/BaseHandshake.h"
#include "Kitsune/Net/Datagram.h"
#include "Kitsune/Net/Describe.h"
#include "Kitsune/Net/GameEvent.h"
#include "Kitsune/Net/GameSetup.h"
#include "Kitsune/Net/KeepAlive.h"
#include "Kitsune/Net/LocalNet.h"
#include "Kitsune/Net/LocalEvent.h"
#include "Kitsune/Net/Lockstep.h"
#include "Kitsune/Net/MessageChannel.h"
#include "Kitsune/Net/PeerSession.h"
#include "Kitsune/Net/Ports.h"
#include "Kitsune/Net/SystemEvent.h"
#include "Kitsune/Net/TurnChannel.h"
#include "Kitsune/Net/UdpJoin.h"
#include "Kitsune/Version.h"
