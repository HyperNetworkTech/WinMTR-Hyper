#pragma once

#include <stop_token>
#include <string>
#include <vector>

namespace winmtr::network_data {

struct IpConnectionInfo final {
	std::wstring address;
	std::wstring hostname;
	std::wstring city;
	std::wstring region;
	std::wstring country;
	std::wstring countryCode;
	std::wstring asn;
	std::wstring isp;
	std::wstring source;

	[[nodiscard]] bool available() const noexcept { return !address.empty(); }
};

enum class EcsSupport {
	supported,
	unsupported,
	unknown
};

struct DnsConnectionInfo final {
	std::wstring publicAddress;
	std::wstring provider;
	std::wstring location;
	std::wstring ecsSubnet;
	EcsSupport ecsSupport = EcsSupport::unknown;
	std::vector<std::wstring> localServers;
	std::wstring source;
};

struct CurrentNetworkInfo final {
	IpConnectionInfo ipv4;
	IpConnectionInfo ipv6;
	DnsConnectionInfo dns;
	std::vector<std::wstring> successfulSources;
	bool complete = false;

	[[nodiscard]] bool anyAvailable() const noexcept
	{
		return ipv4.available() || ipv6.available() || !dns.publicAddress.empty() || !dns.localServers.empty();
	}
};

// These routines are intentionally synchronous. Callers run them on a background
// std::jthread so Windows 7 does not need the Windows Runtime asynchronous APIs.
[[nodiscard]] CurrentNetworkInfo queryCurrent(std::stop_token stopToken = {});
[[nodiscard]] IpConnectionInfo queryAddress(const std::wstring& address,
	std::stop_token stopToken = {}, bool resolveHostname = true);
[[nodiscard]] std::wstring formatDetails(const CurrentNetworkInfo& info);
[[nodiscard]] bool isPublicAddress(const std::wstring& address) noexcept;

} // namespace winmtr::network_data
