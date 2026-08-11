#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace winmtr::probe_parameters {

struct WireOptions final {
	std::uint8_t ttl = 0;
	std::uint8_t tos = 0;
	bool dont_fragment = false;
};

[[nodiscard]] constexpr WireOptions make_wire_options(unsigned ttl, unsigned tos,
	bool request_dont_fragment, bool ipv4) noexcept
{
	return WireOptions{
		.ttl = static_cast<std::uint8_t>(std::min(ttl, 255u)),
		.tos = static_cast<std::uint8_t>(std::min(tos, 255u)),
		.dont_fragment = ipv4 && request_dont_fragment,
	};
}

[[nodiscard]] inline std::vector<std::byte> make_payload(unsigned packet_size,
	int payload_pattern, std::uint64_t& random_state)
{
	std::vector<std::byte> payload(packet_size);
	if (payload_pattern >= 0) {
		std::fill(payload.begin(), payload.end(),
			static_cast<std::byte>(static_cast<unsigned>(payload_pattern) & 0xffu));
		return payload;
	}

	// Deterministic xorshift is sufficient for varying diagnostic payload bytes;
	// this is intentionally not a cryptographic random-number generator.
	for (auto& value : payload) {
		random_state ^= random_state << 13u;
		random_state ^= random_state >> 7u;
		random_state ^= random_state << 17u;
		value = static_cast<std::byte>(random_state & 0xffu);
	}
	return payload;
}

} // namespace winmtr::probe_parameters
