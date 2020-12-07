#include "Server.hpp"
#include <Fatal.hpp>
#include <Log.hpp>
#include "Console.hpp"

namespace converge {
    const char* reasonStrs[] = {
        "Unknown",
        "Server Full",
        "Kicked",
        "Server Error",
        "Client Error",
        "Server Shutdown",
        "Player Leaving"
    };

    Server::Server() 
        : connectCallback(nullptr)
        , disconnectCallback(nullptr) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            players[i].present = false;
            players[i].idx = i;
            players[i].peer = nullptr;
        }

        worlds::g_console->registerCommand([&](void*, const char* arg) {
            if (strlen(arg) == 0) {
                logErr("missing ID to kick");
                return;
            }
            int id = atoi(arg);

            if (id > MAX_PLAYERS || id < 0 || !players[id].present) {
                logErr("invalid player ID");
                return;
            }
                
            enet_peer_disconnect(players[id].peer, DisconnectReason_ServerShutdown);
        }, "server_kick", "Kicks a player.", nullptr);
    }

    Server::~Server() {
        stop();
    }

    void Server::start() {
        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = 3011;
        host = enet_host_create(&address, MAX_PLAYERS, 2, 0, 0);

        if (host == NULL) {
            fatalErr("An error occurred while trying to create an ENet server host.");
        }

        worlds::g_console->registerCommand([&](void*, const char*) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].peer) {
                    logMsg("player %i: present %i, %u RTT", i, players[i].present, players[i].peer->roundTripTime);
                } 
            }
        }, "list", "List players.", nullptr);
    }

    bool Server::findFreePlayerSlot(uint8_t& slot) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].present) {
                slot = i;
                return true;
            }
        }

        // no free slots, server is full
        return false;
    }

    void Server::handleReceivedPacket(const ENetEvent& evt, MessageCallback callback) {
        logMsg("recieved packet from %lu", (uintptr_t)evt.peer->data);
        if (evt.packet->data[0] == MessageType::JoinRequest) {
            msgs::PlayerJoinRequest pjr;
            pjr.fromPacket(evt.packet);

            logMsg("pjr: auth id: %lu, auth universe: %u, version %lu", 
                    pjr.userAuthId, pjr.userAuthUniverse, pjr.gameVersion);

            // reply with accept
            msgs::PlayerJoinAcceptance pja;
            pja.serverSideID = (uint16_t)(uintptr_t)evt.peer->data;

            ENetPacket* pjaPacket = pja.toPacket(ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(evt.peer, 0, pjaPacket);

            enet_packet_destroy(evt.packet);
            return;
        }

        NetBase::handleReceivedPacket(evt, callback);
    }

    void Server::handleConnection(const ENetEvent& evt) {
        logMsg("received new connection");
        // allocate new player
        uint8_t newIdx;

        if (!findFreePlayerSlot(newIdx)) {
            logWarn("rejecting connection as server is full :(");
            enet_peer_disconnect(evt.peer, DisconnectReason_ServerFull);
            return;
        }

        logMsg("new player has idx of %i", newIdx);

        // start setting up stuff for the player
        players[newIdx].peer = evt.peer;
        players[newIdx].present = true;

        if (connectCallback)
            connectCallback(players[newIdx], callbackCtx);

        evt.peer->data = (void*)(uintptr_t)newIdx;
    }

    void Server::handleDisconnection(const ENetEvent& evt) {
        if (evt.data < sizeof(reasonStrs) / sizeof(reasonStrs[0]) && evt.data > 0) {
            logMsg("received disconnection. reason: %i (%s)", evt.data, reasonStrs[evt.data]);
        } else {
            logMsg("received disconnection. reason: %i", evt.data);
        }
        uint8_t idx = (uint8_t)(uintptr_t)evt.peer->data;

        if (!players[idx].present) {
            return;
        }

        if (disconnectCallback)
            disconnectCallback(players[idx], callbackCtx);
        players[idx].present = false;
    }

    void Server::stop() {
        logMsg("server stopping");
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].present) {
                enet_peer_disconnect(players[i].peer, DisconnectReason_ServerShutdown);
            }
        }

        processMessages(nullptr);
        enet_host_destroy(host);
        host = nullptr;
    }
}
