/*
WinMTR
Copyright (C)  2010-2019 Appnor MSP S.A. - http://www.appnor.com
Copyright (C) 2019-2026 Leetsoftwerx

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.
*/

module;
#pragma warning (disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMCX
#define NOIME
#define NOGDI
#define NONLS
#define NOAPISET
#define NOSERVICE
#define NOMINMAX
#include <winsock2.h>
#include <WS2tcpip.h>
#include "WinMTRICMPPIOdef.h"
#include "WinMTRNetworkData.h"
module WinMTR.Net:Tracing;

import <algorithm>;
import <array>;
import <chrono>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <iterator>;
import <memory>;
import <string>;
import <thread>;
import <utility>;
import <vector>;
import WinMTRIPUtils;
import WinMTRSNetHost;
import WinMTRUtils;
import :ClassDef;

namespace {

class unique_icmp_handle final {
public:
	unique_icmp_handle() noexcept = default;
	explicit unique_icmp_handle(HANDLE value) noexcept : value_(value) {}
	~unique_icmp_handle() noexcept { reset(); }
	unique_icmp_handle(const unique_icmp_handle&) = delete;
	unique_icmp_handle& operator=(const unique_icmp_handle&) = delete;
	unique_icmp_handle(unique_icmp_handle&& other) noexcept
		: value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
	unique_icmp_handle& operator=(unique_icmp_handle&& other) noexcept
	{
		if (this != &other) {
			reset();
			value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
		}
		return *this;
	}
	[[nodiscard]] HANDLE get() const noexcept { return value_; }
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
	}
private:
	void reset() noexcept
	{
		if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
			IcmpCloseHandle(value_);
		}
		value_ = INVALID_HANDLE_VALUE;
	}
	HANDLE value_ = INVALID_HANDLE_VALUE;
};

struct pending_probe final {
	unsigned ttl = 0;
	IP_OPTION_INFORMATION ip_options = {};
	sockaddr_in6 ipv6_source = {};
	std::vector<std::byte> reply_buffer;
	DWORD synchronous_reply_count = 0;
	DWORD issue_error = ERROR_SUCCESS;
	std::uint64_t completed_tick = 0;
	bool issued = false;
	bool usable_reply = false;
	bool destination_reply = false;
};

struct parsed_reply final {
	SOCKADDR_INET address = {};
	unsigned round_trip_ms = 0;
	DWORD status = IP_GENERAL_FAILURE;
};

[[nodiscard]] bool is_usable_trace_reply(DWORD status) noexcept
{
	// IcmpParseReplies returning a record means an ICMP response arrived.  Keep
	// remote unreachable/packet-too-big/parameter responses as real nodes; only
	// a completed no-reply timeout or an unusable local failure is packet loss.
	return status != IP_REQ_TIMED_OUT && status != IP_GENERAL_FAILURE;
}

[[nodiscard]] std::size_t reply_buffer_size(ADDRESS_FAMILY family, std::size_t request_size) noexcept
{
	const auto reply_header = family == AF_INET
		? sizeof(ICMP_ECHO_REPLY)
		: sizeof(ICMPV6_ECHO_REPLY);
	return reply_header + sizeof(IO_STATUS_BLOCK) + request_size + 8u;
}

[[nodiscard]] std::vector<std::byte> make_payload(const WinMTRTraceOptions& options,
	std::uint64_t& random_state)
{
	std::vector<std::byte> payload(options.packet_size);
	if (options.payload_pattern >= 0) {
		std::fill(payload.begin(), payload.end(),
			static_cast<std::byte>(options.payload_pattern));
		return payload;
	}

	// Small xorshift generator: no runtime dependency and random mode changes
	// every byte without claiming cryptographic randomness.
	for (auto& value : payload) {
		random_state ^= random_state << 13u;
		random_state ^= random_state >> 7u;
		random_state ^= random_state << 17u;
		value = static_cast<std::byte>(random_state & 0xffu);
	}
	return payload;
}

[[nodiscard]] unique_icmp_handle create_icmp_handle(ADDRESS_FAMILY family) noexcept
{
	if (family == AF_INET) {
		return unique_icmp_handle{ IcmpCreateFile() };
	}
	if (family == AF_INET6) {
		return unique_icmp_handle{ Icmp6CreateFile() };
	}
	return {};
}

void issue_probe(pending_probe& probe, HANDLE icmp_handle,
	const SOCKADDR_INET& destination, const WinMTRTraceOptions& options,
	const std::vector<std::byte>& payload, unsigned timeout_ms)
{
	if (icmp_handle == nullptr || icmp_handle == INVALID_HANDLE_VALUE) {
		probe.issue_error = ERROR_INVALID_HANDLE;
		return;
	}

	probe.reply_buffer.resize(reply_buffer_size(destination.si_family, payload.size()));
	probe.ip_options.Ttl = static_cast<UCHAR>(probe.ttl);
	probe.ip_options.Tos = static_cast<UCHAR>(options.tos);
	probe.ip_options.Flags = destination.si_family == AF_INET && options.dont_fragment
		? IP_FLAG_DF
		: 0;

	DWORD result = 0;
	SetLastError(ERROR_SUCCESS);
	if (destination.si_family == AF_INET) {
		result = IcmpSendEcho2Ex(
			icmp_handle,
			nullptr,
			nullptr,
			nullptr,
			ADDR_ANY,
			destination.Ipv4.sin_addr.S_un.S_addr,
			payload.empty() ? nullptr : const_cast<std::byte*>(payload.data()),
			static_cast<WORD>(payload.size()),
			&probe.ip_options,
			probe.reply_buffer.data(),
			static_cast<DWORD>(probe.reply_buffer.size()),
			timeout_ms);
	}
	else if (destination.si_family == AF_INET6) {
		probe.ipv6_source = {};
		probe.ipv6_source.sin6_family = AF_INET6;
		result = Icmp6SendEcho2(
			icmp_handle,
			nullptr,
			nullptr,
			nullptr,
			&probe.ipv6_source,
			const_cast<sockaddr_in6*>(&destination.Ipv6),
			payload.empty() ? nullptr : const_cast<std::byte*>(payload.data()),
			static_cast<WORD>(payload.size()),
			&probe.ip_options,
			probe.reply_buffer.data(),
			static_cast<DWORD>(probe.reply_buffer.size()),
			timeout_ms);
	}
	else {
		probe.issue_error = WSAEAFNOSUPPORT;
		return;
	}

	probe.completed_tick = GetTickCount64();
	probe.issued = true;
	if (result != 0) {
		probe.synchronous_reply_count = result;
		return;
	}

	probe.issue_error = GetLastError();
	if (probe.issue_error != IP_REQ_TIMED_OUT) {
		// Invalid parameters, unavailable protocol stacks and local resource
		// failures never issued a usable probe and therefore are not loss.
		probe.issued = false;
	}
}

[[nodiscard]] parsed_reply parse_reply(const pending_probe& probe, ADDRESS_FAMILY family) noexcept
{
	parsed_reply parsed;
	if (!probe.issued || probe.reply_buffer.empty()) {
		return parsed;
	}

	DWORD reply_count = probe.synchronous_reply_count;
	if (reply_count == 0) {
		reply_count = family == AF_INET
			? IcmpParseReplies(const_cast<std::byte*>(probe.reply_buffer.data()),
				static_cast<DWORD>(probe.reply_buffer.size()))
			: Icmp6ParseReplies(const_cast<std::byte*>(probe.reply_buffer.data()),
				static_cast<DWORD>(probe.reply_buffer.size()));
	}
	if (reply_count == 0) {
		return parsed;
	}

	if (family == AF_INET) {
		const auto* reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(probe.reply_buffer.data());
		parsed.status = reply->Status;
		parsed.round_trip_ms = reply->RoundTripTime;
		parsed.address.Ipv4 = {};
		parsed.address.Ipv4.sin_family = AF_INET;
		parsed.address.Ipv4.sin_addr.S_un.S_addr = reply->Address;
	}
	else {
		const auto* reply = reinterpret_cast<const ICMPV6_ECHO_REPLY*>(probe.reply_buffer.data());
		parsed.status = reply->Status;
		parsed.round_trip_ms = reply->RoundTripTime;
		parsed.address.Ipv6 = {};
		parsed.address.Ipv6.sin6_family = AF_INET6;
		parsed.address.Ipv6.sin6_scope_id = reply->Address.sin6_scope_id;
		std::memcpy(&parsed.address.Ipv6.sin6_addr, reply->Address.sin6_addr,
			sizeof(parsed.address.Ipv6.sin6_addr));
	}
	return parsed;
}

[[nodiscard]] unsigned derive_path_ceiling(
	const std::array<bool, WinMTRNet::MAX_HOPS>& responded,
	const std::array<bool, WinMTRNet::MAX_HOPS>& destination_replied,
	const WinMTRTraceOptions& options) noexcept
{
	unsigned first_destination = 0;
	unsigned highest_response = 0;
	for (unsigned ttl = options.start_ttl; ttl <= options.max_hops; ++ttl) {
		if (responded[ttl - 1]) {
			highest_response = ttl;
		}
		if (first_destination == 0 && destination_replied[ttl - 1]) {
			first_destination = ttl;
		}
	}

	const auto mandatory_ttl = std::max(options.start_ttl, options.minimum_ttl);
	if (first_destination != 0) {
		return std::min(options.max_hops, std::max(first_destination, mandatory_ttl));
	}

	const auto tail_origin = highest_response == 0
		? options.start_ttl - 1
		: highest_response;
	const auto with_unknown_tail = std::min<std::uint64_t>(options.max_hops,
		static_cast<std::uint64_t>(tail_origin) + options.unknown_host_limit);
	return std::max(mandatory_ttl, static_cast<unsigned>(with_unknown_tail));
}

void wait_until_next_cycle(std::stop_token stop_token,
	std::chrono::steady_clock::time_point deadline)
{
	using namespace std::chrono_literals;
	while (!stop_token.stop_requested()) {
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			return;
		}
		std::this_thread::sleep_for(std::min(50ms,
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)));
	}
}

} // namespace

WinMTRTraceResult WinMTRNet::DoTrace(std::stop_token stop_token, SOCKADDR_INET address,
	std::wstring target, bool stop_after_unreached_first_round)
{
	if (!isValidAddress(address)) {
		return WinMTRTraceResult::no_reply;
	}

	bool was_tracing = false;
	if (!tracing.compare_exchange_strong(was_tracing, true, std::memory_order_acq_rel)) {
		return WinMTRTraceResult::no_reply;
	}

	WinMTRTraceOptions trace_options;
	try {
		trace_options = options != nullptr
			? options->snapshotTraceOptions()
			: WinMTRTraceOptions{};
		beginSession(address, std::move(target), trace_options);
	}
	catch (...) {
		// Keep the object restartable even if a target/options allocation fails
		// before the normal session guard can be installed.
		tracing.store(false, std::memory_order_release);
		throw;
	}
	const auto this_session = session_id.load(std::memory_order_acquire);
	// These Windows ICMP calls are synchronous: a silent hop blocks its worker
	// until Timeout expires. Cap that wait at the configured send interval so a
	// 3-second reply timeout cannot turn a 1-second MTR cadence into 3 seconds.
	const auto interval_timeout_ms = std::clamp<unsigned>(
		static_cast<unsigned>(trace_options.interval_seconds * 1000.0 + 0.5),
		WinMTRUtils::MIN_TIMEOUT_MS, WinMTRUtils::MAX_TIMEOUT_MS);
	const unsigned probe_timeout_ms = std::min(trace_options.timeout_ms, interval_timeout_ms);
	struct trace_guard final {
		WinMTRNet* owner;
		std::uint64_t session;
		~trace_guard() noexcept { owner->finishSession(session); }
	} guard{ this, this_session };

	std::vector<unique_icmp_handle> icmp_handles;
	icmp_handles.reserve(trace_options.max_hops);
	for (unsigned ttl = 1; ttl <= trace_options.max_hops; ++ttl) {
		icmp_handles.push_back(create_icmp_handle(address.si_family));
	}

	std::array<bool, MAX_HOPS> known_responded = {};
	std::array<bool, MAX_HOPS> known_destination = {};
	unsigned normal_ceiling = trace_options.max_hops;
	bool force_exploration = true;
	std::uint64_t random_state = GetTickCount64() ^ (this_session * 0x9e3779b97f4a7c15ull);
	std::uint64_t quota_epoch = data_epoch.load(std::memory_order_acquire);
	std::uint64_t completed_for_epoch = 0;
	std::uint64_t cycle_serial = 0;
	bool session_reached_destination = false;
	bool session_had_usable_reply = false;

	while (!stop_token.stop_requested()
		&& (trace_options.cycles == 0 || completed_for_epoch < trace_options.cycles)) {
		const auto cycle_started = std::chrono::steady_clock::now();
		const auto round_epoch = data_epoch.load(std::memory_order_acquire);
		if (round_epoch != quota_epoch) {
			quota_epoch = round_epoch;
			completed_for_epoch = 0;
			known_responded.fill(false);
			known_destination.fill(false);
			normal_ceiling = trace_options.max_hops;
			force_exploration = true;
		}
		++cycle_serial;
		const auto round_number = completed_for_epoch + 1;
		const bool exploration_round = force_exploration
			|| round_number == 1
			|| ((round_number - 1) % WinMTRUtils::PATH_EXPLORATION_PERIOD == 0);
		force_exploration = false;

		std::array<bool, MAX_HOPS> round_responded = {};
		std::array<bool, MAX_HOPS> round_destination = {};
		auto payload = make_payload(trace_options, random_state);
		bool round_had_completed_probe = false;
		const bool has_known_destination = std::any_of(known_destination.begin(),
			known_destination.end(), [](bool value) { return value; });
		const unsigned round_ceiling = exploration_round && !has_known_destination
			? trace_options.max_hops
			: normal_ceiling;

		std::vector<pending_probe> probes;
		probes.reserve(round_ceiling - trace_options.start_ttl + 1u);
		const auto cache_tick = GetTickCount64();
		for (unsigned ttl = trace_options.start_ttl; ttl <= round_ceiling; ++ttl) {
			bool cached_destination = false;
			if (replyIsCached(ttl, cache_tick, trace_options.reply_cache_seconds,
				round_epoch, cached_destination)) {
				round_responded[ttl - 1] = true;
				round_destination[ttl - 1] = cached_destination;
				continue;
			}

			pending_probe probe;
			probe.ttl = ttl;
			probes.push_back(std::move(probe));
		}

		// Windows' event/APC ICMP variants do not apply Timeout asynchronously.
		// Run one synchronous call per TTL in parallel so each cycle (including
		// Stop draining) is bounded by one configured timeout, off the UI thread.
		// Every worker owns a distinct reply buffer.
		{
			std::vector<std::jthread> workers;
			workers.reserve(probes.size());
			for (auto& probe : probes) {
				auto* pending = &probe;
				const HANDLE handle = icmp_handles[probe.ttl - 1].get();
				workers.emplace_back([this, pending, handle, &address, &trace_options, &payload,
					probe_timeout_ms, cycle_serial, this_session, round_epoch] {
					issue_probe(*pending, handle, address, trace_options, payload, probe_timeout_ms);
					if (!pending->issued) return;
					const auto parsed = parse_reply(*pending, address.si_family);
					if (is_usable_trace_reply(parsed.status) && isValidAddress(parsed.address)) {
						pending->usable_reply = true;
						pending->destination_reply = parsed.status == IP_SUCCESS
							&& same_network_address(parsed.address, address);
						commitReply(pending->ttl, parsed.address, parsed.round_trip_ms, cycle_serial,
							pending->completed_tick, this_session, round_epoch,
							pending->destination_reply, trace_options.resolve_hostnames,
							trace_options.lookup_asn_isp);
					}
					else {
						commitTimeout(pending->ttl, round_epoch);
					}
					if (options != nullptr) options->notifyTraceDataChanged();
				});
			}
			// jthread destruction drains the complete batch. Pending probes are not
			// visible as packet loss until their call completes or times out.
		}
		for (const auto& probe : probes) {
			if (!probe.issued) continue;
			round_had_completed_probe = true;
			if (probe.usable_reply) {
				round_responded[probe.ttl - 1] = true;
				round_destination[probe.ttl - 1] = probe.destination_reply;
				session_had_usable_reply = true;
				session_reached_destination = session_reached_destination || probe.destination_reply;
			}
		}

		bool round_accepted = false;
		if (data_epoch.load(std::memory_order_acquire) == round_epoch) {
			const bool round_has_destination = std::any_of(round_destination.begin(),
				round_destination.end(), [](bool value) { return value; });
			const bool previously_had_destination = std::any_of(known_destination.begin(),
				known_destination.end(), [](bool value) { return value; });
			auto current_path = known_responded;
			if (exploration_round || completed_for_epoch == 0) {
				// A full-path exploration is the authoritative current route.  This
				// lets a formerly long path shrink instead of retaining a permanent
				// historical response beyond the configured unknown-tail limit.
				current_path = round_responded;
			}
			else {
				for (size_t index = 0; index < current_path.size(); ++index) {
					current_path[index] = current_path[index] || round_responded[index];
				}
			}
			auto destination_path = known_destination;
			if (round_has_destination) {
				destination_path = round_destination;
			}
			else if (exploration_round || previously_had_destination) {
				destination_path.fill(false);
			}
			const unsigned next_ceiling = derive_path_ceiling(current_path,
				destination_path, trace_options);
			std::scoped_lock lock(ghMutex);
			if (data_epoch.load(std::memory_order_relaxed) == round_epoch) {
				known_responded = current_path;
				known_destination = destination_path;
				normal_ceiling = next_ceiling;
				if (!exploration_round && previously_had_destination && !round_has_destination) {
					force_exploration = true;
				}
				if (round_had_completed_probe || display_max_ttl != 0) {
					display_max_ttl = std::clamp(normal_ceiling,
						session_start_ttl, session_options.max_hops);
				}
				++completed_for_epoch;
				completed_cycles = completed_for_epoch;
				++data_revision;
				round_accepted = true;
			}
		}
		if (!round_accepted) {
			quota_epoch = data_epoch.load(std::memory_order_acquire);
			completed_for_epoch = 0;
			known_responded.fill(false);
			known_destination.fill(false);
			normal_ceiling = trace_options.max_hops;
			force_exploration = true;
			continue;
		}

		if (stop_token.stop_requested()
			|| (stop_after_unreached_first_round && completed_for_epoch == 1
				&& !session_reached_destination)
			|| (trace_options.cycles != 0 && completed_for_epoch >= trace_options.cycles)) {
			break;
		}
		const auto interval = std::chrono::duration<double>(trace_options.interval_seconds);
		wait_until_next_cycle(stop_token, cycle_started
			+ std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval));
	}
	if (session_reached_destination) return WinMTRTraceResult::destination;
	if (session_had_usable_reply) return WinMTRTraceResult::reply;
	return WinMTRTraceResult::no_reply;
}

void WinMTRNet::beginSession(const SOCKADDR_INET& address, std::wstring target,
	const WinMTRTraceOptions& trace_options)
{
	std::scoped_lock lock(ghMutex);
	session_id.fetch_add(1, std::memory_order_acq_rel);
	data_epoch.fetch_add(1, std::memory_order_acq_rel);
	last_remote_addr = address;
	target_name = target.empty() ? addr_to_string(address) : std::move(target);
	session_options = trace_options;
	session_start_ttl = trace_options.start_ttl;
	display_max_ttl = 0;
	reply_sequence = 0;
	completed_cycles = 0;
	++data_revision;
	for (unsigned index = 0; index < host.size(); ++index) {
		host[index].reset(index + 1);
	}
}

void WinMTRNet::finishSession(std::uint64_t expected_session) noexcept
{
	if (session_id.load(std::memory_order_acquire) == expected_session) {
		tracing.store(false, std::memory_order_release);
	}
}

void WinMTRNet::ResetHops() noexcept
{
	std::scoped_lock lock(ghMutex);
	data_epoch.fetch_add(1, std::memory_order_acq_rel);
	display_max_ttl = 0;
	reply_sequence = 0;
	completed_cycles = 0;
	++data_revision;
	for (unsigned index = 0; index < host.size(); ++index) {
		host[index].reset(index + 1);
	}
}

void WinMTRNet::setDisplayMaximum(unsigned ttl, std::uint64_t expected_epoch) noexcept
{
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
		return;
	}
	display_max_ttl = std::max(display_max_ttl,
		std::clamp(ttl, session_start_ttl, session_options.max_hops));
	++data_revision;
}

bool WinMTRNet::replyIsCached(unsigned ttl, std::uint64_t now_tick,
	unsigned cache_seconds, std::uint64_t expected_epoch,
	bool& is_destination) const noexcept
{
	is_destination = false;
	if (cache_seconds == 0 || ttl == 0 || ttl > host.size()) {
		return false;
	}
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
		return false;
	}
	const auto& hop = host[ttl - 1];
	if (hop.last_reply_tick == 0 || now_tick < hop.last_reply_tick
		|| now_tick - hop.last_reply_tick > static_cast<std::uint64_t>(cache_seconds) * 1000ull) {
		return false;
	}
	is_destination = hop.last_destination_reply_tick != 0
		&& now_tick >= hop.last_destination_reply_tick
		&& now_tick - hop.last_destination_reply_tick
			<= static_cast<std::uint64_t>(cache_seconds) * 1000ull;
	return true;
}

void WinMTRNet::commitTimeout(unsigned ttl, std::uint64_t expected_epoch) noexcept
{
	if (ttl == 0 || ttl > host.size()) {
		return;
	}
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
		return;
	}
	host[ttl - 1].noteTimeout();
	// Make this completed probe visible immediately. During the first round the
	// snapshot getter still limits the table to the contiguous completed prefix,
	// so a faster high-TTL timeout cannot expose unfinished hops as packet loss.
	display_max_ttl = std::max(display_max_ttl,
		std::clamp(ttl, session_start_ttl, session_options.max_hops));
	++data_revision;
}

void WinMTRNet::commitReply(unsigned ttl, const SOCKADDR_INET& responder,
	unsigned round_trip_ms, std::uint64_t cycle, std::uint64_t tick,
	std::uint64_t expected_session, std::uint64_t expected_epoch,
	bool is_destination, bool resolve_hostname, bool lookup_asn_isp)
{
	if (ttl == 0 || ttl > host.size() || !isValidAddress(responder)) {
		return;
	}

	bool needs_reverse_lookup = false;
	{
		std::scoped_lock lock(ghMutex);
		if (session_id.load(std::memory_order_relaxed) != expected_session
			|| data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
			return;
		}
		auto& hop = host[ttl - 1];
		hop.noteReply(round_trip_ms, cycle, tick);
		display_max_ttl = std::max(display_max_ttl,
			std::clamp(ttl, session_start_ttl, session_options.max_hops));
		if (is_destination) hop.last_destination_reply_tick = tick;
		auto& observed = hop.observeResponder(responder, ++reply_sequence, tick);
		const auto address_key = addr_to_string(responder);
		auto cached = responder_lookup_cache.find(address_key);
		const auto now = GetTickCount64();
		const auto cacheLifetime = static_cast<std::uint64_t>(
			WinMTRUtils::RESPONDER_METADATA_CACHE_SECONDS) * 1000ull;
		if (cached != responder_lookup_cache.end()
			&& (cached->second.cached_at_tick == 0 || now < cached->second.cached_at_tick
				|| now - cached->second.cached_at_tick > cacheLifetime)) {
			responder_lookup_cache.erase(cached);
			cached = responder_lookup_cache.end();
		}
		if (cached != responder_lookup_cache.end()) {
			const std::wstring displayed_name = resolve_hostname ? cached->second.name : L"";
			const std::wstring displayed_country = lookup_asn_isp ? cached->second.country : L"";
			const std::wstring displayed_asn = lookup_asn_isp ? cached->second.asn : L"";
			const std::wstring displayed_isp = lookup_asn_isp ? cached->second.isp : L"";
			observed.name = displayed_name;
			hop.updateResponder(responder, displayed_name, displayed_country,
				displayed_asn, displayed_isp);
			needs_reverse_lookup = (resolve_hostname && !cached->second.hostname_queried)
				|| (lookup_asn_isp && !cached->second.network_queried);
		}
		else {
			needs_reverse_lookup = resolve_hostname || lookup_asn_isp;
		}
		++data_revision;
	}

	if (needs_reverse_lookup) {
		scheduleReverseLookup(responder, expected_session, expected_epoch,
			resolve_hostname, lookup_asn_isp);
	}
}

void WinMTRNet::scheduleReverseLookup(const SOCKADDR_INET& address,
	std::uint64_t expected_session, std::uint64_t expected_epoch,
	bool resolve_hostname, bool lookup_asn_isp)
{
	const auto address_key = addr_to_string(address);
	if (address_key.empty()) {
		return;
	}
	const auto lookup_token = std::to_wstring(expected_session) + L":"
		+ std::to_wstring(expected_epoch) + L":" + address_key;
	{
		std::scoped_lock lock(ghMutex);
		if (session_id.load(std::memory_order_relaxed) != expected_session
			|| data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
			return;
		}
		auto cached = responder_lookup_cache.find(address_key);
		const auto now = GetTickCount64();
		const auto cacheLifetime = static_cast<std::uint64_t>(
			WinMTRUtils::RESPONDER_METADATA_CACHE_SECONDS) * 1000ull;
		if (cached != responder_lookup_cache.end()
			&& (cached->second.cached_at_tick == 0 || now < cached->second.cached_at_tick
				|| now - cached->second.cached_at_tick > cacheLifetime)) {
			responder_lookup_cache.erase(cached);
			cached = responder_lookup_cache.end();
		}
		if (cached != responder_lookup_cache.end()) {
			const std::wstring displayed_name = resolve_hostname ? cached->second.name : L"";
			const std::wstring displayed_country = lookup_asn_isp ? cached->second.country : L"";
			const std::wstring displayed_asn = lookup_asn_isp ? cached->second.asn : L"";
			const std::wstring displayed_isp = lookup_asn_isp ? cached->second.isp : L"";
			for (auto& hop : host) {
				hop.updateResponder(address, displayed_name, displayed_country,
					displayed_asn, displayed_isp);
			}
			if ((!resolve_hostname || cached->second.hostname_queried)
				&& (!lookup_asn_isp || cached->second.network_queried)) {
				return;
			}
		}
		if (!reverse_dns_inflight.insert(lookup_token).second) {
			return;
		}
	}

	auto self = weak_from_this().lock();
	if (!self) {
		std::scoped_lock lock(ghMutex);
		reverse_dns_inflight.erase(lookup_token);
		return;
	}
	std::thread([self = std::move(self), address, address_key, lookup_token,
		expected_session, expected_epoch, resolve_hostname, lookup_asn_isp]() mutable {
		wchar_t buffer[NI_MAXHOST] = {};
		responder_metadata metadata;
		metadata.hostname_queried = resolve_hostname;
		metadata.network_queried = lookup_asn_isp;
		metadata.cached_at_tick = GetTickCount64();
		if (lookup_asn_isp && winmtr::network_data::isPublicAddress(address_key)) {
			const auto network_data = winmtr::network_data::queryAddress(address_key, {}, resolve_hostname);
			metadata.country = network_data.country;
			metadata.asn = network_data.asn;
			metadata.isp = network_data.isp;
			if (resolve_hostname) {
				metadata.name = network_data.hostname;
			}
		}
		if (resolve_hostname && metadata.name.empty()) {
			auto local_address = address;
			if (GetNameInfoW(reinterpret_cast<const sockaddr*>(&local_address),
				static_cast<socklen_t>(getAddressSize(local_address)),
				buffer, static_cast<DWORD>(std::size(buffer)), nullptr, 0,
				NI_NAMEREQD) == 0) {
				metadata.name = buffer;
			}
		}

		std::scoped_lock lock(self->ghMutex);
		self->reverse_dns_inflight.erase(lookup_token);
		if (self->session_id.load(std::memory_order_relaxed) != expected_session
			|| self->data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
			return;
		}
		if (const auto previous = self->responder_lookup_cache.find(address_key);
			previous != self->responder_lookup_cache.end()) {
			if (metadata.name.empty()) metadata.name = previous->second.name;
			if (metadata.country.empty()) metadata.country = previous->second.country;
			if (metadata.asn.empty()) metadata.asn = previous->second.asn;
			if (metadata.isp.empty()) metadata.isp = previous->second.isp;
			metadata.hostname_queried = metadata.hostname_queried || previous->second.hostname_queried;
			metadata.network_queried = metadata.network_queried || previous->second.network_queried;
		}
		if (!self->responder_lookup_cache.contains(address_key)
			&& self->responder_lookup_cache.size()
				>= WinMTRUtils::MAX_RESPONDER_METADATA_CACHE_ENTRIES) {
			const auto oldest = std::min_element(self->responder_lookup_cache.begin(),
				self->responder_lookup_cache.end(), [](const auto& lhs, const auto& rhs) {
					return lhs.second.cached_at_tick < rhs.second.cached_at_tick;
				});
			if (oldest != self->responder_lookup_cache.end()) {
				self->responder_lookup_cache.erase(oldest);
			}
		}
		self->responder_lookup_cache[address_key] = metadata;
		if (metadata.name.empty() && metadata.country.empty()
			&& metadata.asn.empty() && metadata.isp.empty()) {
			return;
		}
		bool updated = false;
		const std::wstring displayed_name = resolve_hostname ? metadata.name : L"";
		const std::wstring displayed_country = lookup_asn_isp ? metadata.country : L"";
		const std::wstring displayed_asn = lookup_asn_isp ? metadata.asn : L"";
		const std::wstring displayed_isp = lookup_asn_isp ? metadata.isp : L"";
		for (auto& hop : self->host) {
			updated = hop.updateResponder(address, displayed_name, displayed_country,
				displayed_asn, displayed_isp) || updated;
		}
		if (updated) ++self->data_revision;
	}).detach();
}

bool WinMTRNet::updateResponderMetadata(std::uint64_t expected_session,
	std::uint64_t expected_epoch, const SOCKADDR_INET& address,
	std::wstring name, std::wstring country, std::wstring asn, std::wstring isp)
{
	std::scoped_lock lock(ghMutex);
	if (session_id.load(std::memory_order_relaxed) != expected_session
		|| data_epoch.load(std::memory_order_relaxed) != expected_epoch) {
		return false;
	}
	bool updated = false;
	for (auto& hop : host) {
		updated = hop.updateResponder(address, name, country, asn, isp) || updated;
	}
	if (updated) ++data_revision;
	return updated;
}
