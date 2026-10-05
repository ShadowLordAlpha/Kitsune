#include "Kitsune/Net/Describe.h"

#include <sstream>

namespace kitsune::net {

std::string describeSystemEvent(const SystemEvent& event) {
    const auto& header = systemHeader(event);
    std::ostringstream line;
    line << "tcp " << systemEventName(systemEventId(event)) << " seq=" << header.sequence
         << " ack=" << header.ackSequence << " peer=" << header.originatingPeer;
    if (const auto* connect = std::get_if<ConnectEvent>(&event)) {
        line << " build=" << connect->version.build << " port=" << connect->listenPort;
    } else if (const auto* turn = std::get_if<TurnEvent>(&event)) {
        line << " sync=" << turn->syncValue << " events=" << turn->gameEventData.size();
    } else if (const auto* text = std::get_if<MessageEvent>(&event)) {
        line << " bytes=" << text->messageData.size();
    } else if (const auto* joined = std::get_if<JoinSucceededEvent>(&event)) {
        line << " users=" << joined->users.size();
    }
    return line.str();
}

std::string describeTcpPayload(std::span<const std::byte> bytes) {
    if (const auto event = decodeSystemEvent(bytes)) {
        return describeSystemEvent(*event);
    }
    return "tcp undecoded bytes=" + std::to_string(bytes.size());
}

} // namespace kitsune::net
