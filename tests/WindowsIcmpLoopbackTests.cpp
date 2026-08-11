#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "WinMTRICMPPIOdef.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

class IcmpHandle final {
public:
	explicit IcmpHandle(HANDLE value) noexcept : value_(value) {}
	~IcmpHandle() noexcept
	{
		if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) IcmpCloseHandle(value_);
	}
	IcmpHandle(const IcmpHandle&) = delete;
	IcmpHandle& operator=(const IcmpHandle&) = delete;
	[[nodiscard]] HANDLE get() const noexcept { return value_; }
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
	}
private:
	HANDLE value_;
};

void test_ipv4_loopback()
{
	IcmpHandle handle(IcmpCreateFile());
	require(static_cast<bool>(handle), "IcmpCreateFile failed for an unprivileged process");
	in_addr destination{};
	require(InetPtonW(AF_INET, L"127.0.0.1", &destination) == 1,
		"could not parse IPv4 loopback");
	std::array<std::uint8_t, 64> payload{};
	std::fill(payload.begin(), payload.end(), std::uint8_t{ 0x20 });
	IP_OPTION_INFORMATION options{};
	options.Ttl = 64;
	options.Tos = 0x2e;
	options.Flags = IP_FLAG_DF;
	std::vector<std::byte> reply(sizeof(ICMP_ECHO_REPLY) + payload.size() + 8u);
	SetLastError(ERROR_SUCCESS);
	const DWORD count = IcmpSendEcho2Ex(handle.get(), nullptr, nullptr, nullptr,
		ADDR_ANY, destination.S_un.S_addr, payload.data(),
		static_cast<WORD>(payload.size()), &options, reply.data(),
		static_cast<DWORD>(reply.size()), 1'000);
	require(count != 0, "IPv4 loopback probe returned no reply");
	// A nonzero synchronous return count means the API already populated parsed
	// ICMP_ECHO_REPLY records; IcmpParseReplies is only for async completion.
	const auto* parsed = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
	require(parsed->Status == IP_SUCCESS, "IPv4 loopback reply was not successful");
	require(parsed->DataSize == payload.size(), "IPv4 loopback payload size changed");
	require(parsed->Data != nullptr
		&& std::equal(payload.begin(), payload.end(),
			static_cast<const std::uint8_t*>(parsed->Data)),
		"IPv4 loopback payload content changed");
}

void test_ipv6_loopback()
{
	IcmpHandle handle(Icmp6CreateFile());
	require(static_cast<bool>(handle), "Icmp6CreateFile failed for an unprivileged process");
	sockaddr_in6 source{};
	source.sin6_family = AF_INET6;
	sockaddr_in6 destination{};
	destination.sin6_family = AF_INET6;
	require(InetPtonW(AF_INET6, L"::1", &destination.sin6_addr) == 1,
		"could not parse IPv6 loopback");
	std::array<std::uint8_t, 64> payload{};
	std::fill(payload.begin(), payload.end(), std::uint8_t{ 0x20 });
	IP_OPTION_INFORMATION options{};
	options.Ttl = 64;
	options.Tos = 0x2e;
	options.Flags = 0; // DF is an IPv4-only option.
	std::vector<std::byte> reply(sizeof(ICMPV6_ECHO_REPLY) + sizeof(IO_STATUS_BLOCK)
		+ payload.size() + 8u);
	SetLastError(ERROR_SUCCESS);
	const DWORD count = Icmp6SendEcho2(handle.get(), nullptr, nullptr, nullptr,
		&source, &destination, payload.data(), static_cast<WORD>(payload.size()),
		&options, reply.data(), static_cast<DWORD>(reply.size()), 1'000);
	require(count != 0, "IPv6 loopback probe returned no reply");
	// As with IPv4, a nonzero synchronous return is already parsed.
	const auto* parsed = reinterpret_cast<const ICMPV6_ECHO_REPLY*>(reply.data());
	require(parsed->Status == IP_SUCCESS, "IPv6 loopback reply was not successful");
}

} // namespace

int main()
{
	try {
		test_ipv4_loopback();
		test_ipv6_loopback();
		std::cout << "Windows ICMP IPv4/IPv6 loopback tests passed.\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << "Windows ICMP loopback test failure: " << error.what()
			<< " (Win32 " << GetLastError() << ")\n";
		return 1;
	}
}
