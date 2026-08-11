module;
#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <ws2ipdef.h>
#include <Windows.h>
#include <winnls.h>

export module WinMTRDnsUtil;

import <algorithm>;
import <cstddef>;
import <cstring>;
import <memory>;
import <optional>;
import <stop_token>;
import <string>;
import <string_view>;
import <utility>;
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

export enum class AddressResolutionStatus {
	success,
	not_found,
	timed_out,
	cancelled,
	failed,
};

export struct AddressResolutionResult final {
	std::vector<SOCKADDR_INET> addresses;
	AddressResolutionStatus status = AddressResolutionStatus::failed;
	int error_code = WSANO_RECOVERY;
};

namespace {

class unique_event final {
public:
	explicit unique_event(HANDLE value = nullptr) noexcept : value_(value) {}
	~unique_event() noexcept { if (value_ != nullptr) CloseHandle(value_); }
	unique_event(const unique_event&) = delete;
	unique_event& operator=(const unique_event&) = delete;
	[[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
	HANDLE value_ = nullptr;
};

[[nodiscard]] std::wstring idn_ascii_name(std::wstring_view name)
{
	const std::wstring stable_name(name);
	if (std::none_of(stable_name.begin(), stable_name.end(),
		[](wchar_t value) noexcept { return value > 0x7f; })) {
		return stable_name;
	}
	const int required = IdnToAscii(0, stable_name.c_str(), -1, nullptr, 0);
	if (required <= 1) return {};
	std::wstring ascii(static_cast<std::size_t>(required), L'\0');
	if (IdnToAscii(0, stable_name.c_str(), -1, ascii.data(), required) != required) return {};
	ascii.resize(static_cast<std::size_t>(required - 1));
	return ascii;
}

template<class T>
[[nodiscard]] std::vector<SOCKADDR_INET> collect_addresses(const T* results)
{
	std::vector<SOCKADDR_INET> addresses;
	for (auto current = results; current != nullptr; current = current->ai_next) {
		const auto address = get_sockaddr_from_addrinfo(current);
		if (!address) continue;
		const auto equal = [&](const SOCKADDR_INET& existing) {
			if (existing.si_family != address->si_family) return false;
			if (existing.si_family == AF_INET) {
				return existing.Ipv4.sin_addr.S_un.S_addr == address->Ipv4.sin_addr.S_un.S_addr;
			}
			return existing.Ipv6.sin6_scope_id == address->Ipv6.sin6_scope_id
				&& std::memcmp(&existing.Ipv6.sin6_addr, &address->Ipv6.sin6_addr,
					sizeof(IN6_ADDR)) == 0;
		};
		if (std::find_if(addresses.begin(), addresses.end(), equal) == addresses.end()) {
			addresses.push_back(*address);
		}
	}
	return addresses;
}

[[nodiscard]] AddressResolutionResult resolution_failure(int error_code,
	bool cancelled = false) noexcept
{
	AddressResolutionStatus status = AddressResolutionStatus::failed;
	if (cancelled || error_code == WSA_E_CANCELLED || error_code == ERROR_CANCELLED) {
		status = AddressResolutionStatus::cancelled;
	}
	else if (error_code == WSAETIMEDOUT || error_code == WAIT_TIMEOUT) {
		status = AddressResolutionStatus::timed_out;
	}
	else if (error_code == WSAHOST_NOT_FOUND || error_code == WSANO_DATA) {
		status = AddressResolutionStatus::not_found;
	}
	return { .status = status, .error_code = error_code };
}

template<class Function>
[[nodiscard]] Function resolve_winsock_function(const char* name) noexcept
{
	Function function = nullptr;
	const auto module = GetModuleHandleW(L"Ws2_32.dll");
	if (module == nullptr) return function;
	const auto procedure = GetProcAddress(module, name);
	static_assert(sizeof(function) == sizeof(procedure));
	std::memcpy(&function, &procedure, sizeof(function));
	return function;
}

} // namespace

// Windows 8+ gets true asynchronous cancellation. Windows 7 uses the same
// bounded GetAddrInfoExW timeout synchronously because its Winsock provider does
// not support overlapped name resolution. In both cases the UI thread remains
// free, and a stopped trace never consumes a result produced after cancellation.
export [[nodiscard]] AddressResolutionResult ResolveAddressesWithDeadline(
	std::wstring_view name, int family, std::stop_token stop_token,
	DWORD timeout_ms = 5'000)
{
	if (stop_token.stop_requested()) {
		return resolution_failure(ERROR_CANCELLED, true);
	}
	const auto stable_name = idn_ascii_name(name);
	if (stable_name.empty()) return resolution_failure(WSAHOST_NOT_FOUND);

	ADDRINFOEXW hints{};
	hints.ai_family = family;
	hints.ai_socktype = SOCK_RAW;
	PADDRINFOEXW raw = nullptr;
	TIMEVAL timeout{};
	timeout.tv_sec = static_cast<long>(timeout_ms / 1'000u);
	timeout.tv_usec = static_cast<long>((timeout_ms % 1'000u) * 1'000u);

	using cancel_function = INT(WSAAPI*)(LPHANDLE);
	using result_function = INT(WSAAPI*)(LPOVERLAPPED);
	const auto cancel_query = resolve_winsock_function<cancel_function>("GetAddrInfoExCancel");
	const auto overlapped_result = resolve_winsock_function<result_function>(
		"GetAddrInfoExOverlappedResult");
	int error = WSANO_RECOVERY;

	if (cancel_query != nullptr && overlapped_result != nullptr) {
		unique_event complete_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		unique_event cancel_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (complete_event.get() == nullptr || cancel_event.get() == nullptr) {
			return resolution_failure(static_cast<int>(GetLastError()));
		}
		WSAOVERLAPPED overlapped{};
		overlapped.hEvent = complete_event.get();
		HANDLE query_handle = nullptr;
		error = GetAddrInfoExW(stable_name.c_str(), nullptr, NS_ALL, nullptr,
			&hints, &raw, &timeout, &overlapped, nullptr, &query_handle);
		if (error == WSA_IO_PENDING) {
			std::stop_callback cancel_callback(stop_token, [&] { SetEvent(cancel_event.get()); });
			const HANDLE events[] = { complete_event.get(), cancel_event.get() };
			const DWORD wait = WaitForMultipleObjects(2, events, FALSE, timeout_ms);
			const bool cancelled = wait == WAIT_OBJECT_0 + 1 || stop_token.stop_requested();
			const bool timed_out = wait == WAIT_TIMEOUT;
			if (cancelled || timed_out || wait == WAIT_FAILED) {
				(void)cancel_query(&query_handle);
				(void)WaitForSingleObject(complete_event.get(), INFINITE);
			}
			error = overlapped_result(&overlapped);
			if (cancelled) {
				if (raw != nullptr) FreeAddrInfoExW(raw);
				return resolution_failure(ERROR_CANCELLED, true);
			}
			if (timed_out) {
				if (raw != nullptr) FreeAddrInfoExW(raw);
				return resolution_failure(WSAETIMEDOUT);
			}
			if (wait == WAIT_FAILED) {
				if (raw != nullptr) FreeAddrInfoExW(raw);
				return resolution_failure(static_cast<int>(GetLastError()));
			}
		}
	}
	else {
		error = GetAddrInfoExW(stable_name.c_str(), nullptr, NS_ALL, nullptr,
			&hints, &raw, &timeout, nullptr, nullptr, nullptr);
	}

	std::unique_ptr<ADDRINFOEXW, addrinfo_deleter> results(raw);
	if (stop_token.stop_requested()) return resolution_failure(ERROR_CANCELLED, true);
	if (error != ERROR_SUCCESS) return resolution_failure(error);
	auto addresses = collect_addresses(results.get());
	if (addresses.empty()) return resolution_failure(WSAHOST_NOT_FOUND);
	return {
		.addresses = std::move(addresses),
		.status = AddressResolutionStatus::success,
		.error_code = ERROR_SUCCESS,
	};
}
