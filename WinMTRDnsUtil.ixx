module;
#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <ws2ipdef.h>

export module WinMTRDnsUtil;

import <algorithm>;
import <cstring>;
import <memory>;
import <optional>;
import <string>;
import <string_view>;
import <vector>;

export template<class T>
std::optional<SOCKADDR_INET> get_sockaddr_from_addrinfo(const T* info)
{
	if (info == nullptr || info->ai_addr == nullptr ||
		(info->ai_family != AF_INET && info->ai_family != AF_INET6)) {
		return std::nullopt;
	}
	SOCKADDR_INET address{};
	std::memcpy(&address, info->ai_addr,
		std::min<size_t>(static_cast<size_t>(info->ai_addrlen), sizeof(address)));
	return address;
}

export struct addrinfo_deleter final {
	void operator()(PADDRINFOEXW value) const noexcept
	{
		if (value != nullptr) FreeAddrInfoExW(value);
	}
};

export struct addrinfo_w_deleter final {
	void operator()(PADDRINFOW value) const noexcept
	{
		if (value != nullptr) FreeAddrInfoW(value);
	}
};

// Run this synchronous Windows 7-compatible resolver on the trace worker.
// Ordering from the resolver is retained, while duplicate addresses are removed.
export [[nodiscard]] std::vector<SOCKADDR_INET> ResolveAddresses(
	std::wstring_view name, int family = AF_UNSPEC)
{
	ADDRINFOW hints{};
	hints.ai_family = family;
	hints.ai_socktype = SOCK_RAW;
	PADDRINFOW raw = nullptr;
	const std::wstring stableName(name);
	if (GetAddrInfoW(stableName.c_str(), nullptr, &hints, &raw) != 0) return {};
	std::unique_ptr<ADDRINFOW, addrinfo_w_deleter> results(raw);
	std::vector<SOCKADDR_INET> addresses;
	for (auto current = results.get(); current != nullptr; current = current->ai_next) {
		const auto address = get_sockaddr_from_addrinfo(current);
		if (!address) continue;
		const auto equal = [&](const SOCKADDR_INET& existing) {
			if (existing.si_family != address->si_family) return false;
			if (existing.si_family == AF_INET) {
				return existing.Ipv4.sin_addr.S_un.S_addr == address->Ipv4.sin_addr.S_un.S_addr;
			}
			return std::memcmp(&existing.Ipv6.sin6_addr, &address->Ipv6.sin6_addr,
				sizeof(IN6_ADDR)) == 0;
		};
		if (std::find_if(addresses.begin(), addresses.end(), equal) == addresses.end()) {
			addresses.push_back(*address);
		}
	}
	return addresses;
}
