#pragma once

#include <array>
#include <cstdint>

namespace winmtr::address_policy {

struct Ipv4Prefix final {
	std::uint32_t network;
	std::uint32_t mask;
};

inline constexpr std::array non_public_ipv4{
	Ipv4Prefix{ 0x00000000u, 0xff000000u }, // 0.0.0.0/8 (this network)
	Ipv4Prefix{ 0x0a000000u, 0xff000000u }, // 10.0.0.0/8 (private)
	Ipv4Prefix{ 0x64400000u, 0xffc00000u }, // 100.64.0.0/10 (shared address space)
	Ipv4Prefix{ 0x7f000000u, 0xff000000u }, // 127.0.0.0/8 (loopback)
	Ipv4Prefix{ 0xa9fe0000u, 0xffff0000u }, // 169.254.0.0/16 (link-local)
	Ipv4Prefix{ 0xac100000u, 0xfff00000u }, // 172.16.0.0/12 (private)
	Ipv4Prefix{ 0xc0000000u, 0xffffff00u }, // 192.0.0.0/24 (protocol assignments)
	Ipv4Prefix{ 0xc0000200u, 0xffffff00u }, // 192.0.2.0/24 (TEST-NET-1)
	Ipv4Prefix{ 0xc0586300u, 0xffffff00u }, // 192.88.99.0/24 (deprecated 6to4 relay)
	Ipv4Prefix{ 0xc0a80000u, 0xffff0000u }, // 192.168.0.0/16 (private)
	Ipv4Prefix{ 0xc6120000u, 0xfffe0000u }, // 198.18.0.0/15 (benchmarking)
	Ipv4Prefix{ 0xc6336400u, 0xffffff00u }, // 198.51.100.0/24 (TEST-NET-2)
	Ipv4Prefix{ 0xcb007100u, 0xffffff00u }, // 203.0.113.0/24 (TEST-NET-3)
	Ipv4Prefix{ 0xe0000000u, 0xe0000000u }, // 224.0.0.0/3 (multicast/reserved)
};

[[nodiscard]] constexpr bool is_public_ipv4(std::uint32_t host_order) noexcept
{
	for (const auto& prefix : non_public_ipv4) {
		if ((host_order & prefix.mask) == prefix.network) return false;
	}
	return true;
}

struct Ipv6Prefix final {
	std::array<std::uint8_t, 16> network;
	unsigned bits;
};

inline constexpr std::array non_public_global_ipv6{
	Ipv6Prefix{ { 0x20, 0x01, 0x00, 0x02 }, 48 }, // 2001:2::/48 (benchmarking)
	Ipv6Prefix{ { 0x20, 0x01, 0x00, 0x10 }, 28 }, // 2001:10::/28 (ORCHIDv1)
	Ipv6Prefix{ { 0x20, 0x01, 0x00, 0x20 }, 28 }, // 2001:20::/28 (ORCHIDv2)
	Ipv6Prefix{ { 0x20, 0x01, 0x0d, 0xb8 }, 32 }, // 2001:db8::/32 (documentation)
	Ipv6Prefix{ { 0x3f, 0xff, 0x00 }, 20 },       // 3fff::/20 (documentation)
};

[[nodiscard]] constexpr bool matches(const std::array<std::uint8_t, 16>& address,
	const Ipv6Prefix& prefix) noexcept
{
	const unsigned completeBytes = prefix.bits / 8u;
	const unsigned partialBits = prefix.bits % 8u;
	for (unsigned index = 0; index < completeBytes; ++index) {
		if (address[index] != prefix.network[index]) return false;
	}
	if (partialBits == 0) return true;
	const auto mask = static_cast<std::uint8_t>(0xffu << (8u - partialBits));
	return (address[completeBytes] & mask) == (prefix.network[completeBytes] & mask);
}

[[nodiscard]] constexpr bool is_public_ipv6(
	const std::array<std::uint8_t, 16>& address) noexcept
{
	// External metadata is meaningful only for globally routable unicast space.
	if ((address[0] & 0xe0u) != 0x20u) return false; // outside 2000::/3
	for (const auto& prefix : non_public_global_ipv6) {
		if (matches(address, prefix)) return false;
	}
	return true;
}

} // namespace winmtr::address_policy
