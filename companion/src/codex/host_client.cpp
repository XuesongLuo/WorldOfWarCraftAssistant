#include "wowai/codex/host_client.hpp"

#include <stdexcept>

namespace wowai::codex {

nlohmann::json HostClient::negotiate(const nlohmann::json& hello,
                                     const std::chrono::milliseconds timeout) {
    return exchange(hello, "ready", timeout);
}

nlohmann::json HostClient::request(const nlohmann::json& request,
                                   const std::chrono::milliseconds timeout) {
    return exchange(request, "response", timeout);
}

void HostClient::cancel(const nlohmann::json& cancel) {
    const auto validation = validate_envelope(cancel);
    if (!validation) throw std::invalid_argument(validation.message);
    const auto envelope = cancel.get<ProtocolEnvelope>();
    const auto sequence = sequence_.accept(envelope);
    if (!sequence) throw std::invalid_argument(sequence.message);
    process_.write_line(cancel.dump());
}

nlohmann::json HostClient::exchange(const nlohmann::json& outbound,
                                    const std::string_view expected_kind,
                                    const std::chrono::milliseconds timeout) {
    const auto validation = validate_envelope(outbound);
    if (!validation) throw std::invalid_argument(validation.message);
    const auto outbound_envelope = outbound.get<ProtocolEnvelope>();
    const auto outbound_sequence = sequence_.accept(outbound_envelope);
    if (!outbound_sequence) throw std::invalid_argument(outbound_sequence.message);
    process_.write_line(outbound.dump());

    const auto read = process_.read_line(timeout);
    if (read.status == HostReadStatus::timeout) throw std::runtime_error("Codex Host timed out");
    if (read.status == HostReadStatus::exited) {
        throw std::runtime_error("Codex Host exited with code " + std::to_string(read.exit_code) +
                                 ": " + process_.take_stderr());
    }
    nlohmann::json inbound;
    const auto parsed = parse_json_line(read.line, inbound);
    if (!parsed) throw std::runtime_error(parsed.message);
    const auto inbound_envelope = inbound.get<ProtocolEnvelope>();
    const auto inbound_sequence = sequence_.accept(inbound_envelope);
    if (!inbound_sequence) throw std::runtime_error(inbound_sequence.message);
    if (inbound_envelope.kind != expected_kind && inbound_envelope.kind != "error") {
        throw std::runtime_error("Codex Host returned an unexpected envelope kind");
    }
    return inbound;
}

} // namespace wowai::codex
