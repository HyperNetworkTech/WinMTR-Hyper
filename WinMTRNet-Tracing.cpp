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
#include "WinMTRProbeScheduler.h"
module WinMTR.Net:Tracing;

import <algorithm>;
import <array>;
import <chrono>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <condition_variable>;
import <deque>;
import <iterator>;
import <limits>;
import <memory>;
import <mutex>;
import <string>;
import <system_error>;
import <thread>;
import <unordered_map>;
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

[[nodiscard]] winmtr::probe::MonotonicMilliseconds monotonic_now() noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

[[nodiscard]] std::uint64_t unix_now_ms() noexcept
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}

[[nodiscard]] WinMTRProbeOutcome classify_probe_outcome(DWORD status) noexcept
{
	switch (status) {
	case IP_SUCCESS:
		return WinMTRProbeOutcome::echo_reply;
	case IP_TTL_EXPIRED_TRANSIT:
	case IP_TTL_EXPIRED_REASSEM:
		return WinMTRProbeOutcome::ttl_expired;
	case IP_DEST_NET_UNREACHABLE:
	case IP_DEST_HOST_UNREACHABLE:
	case IP_DEST_PROT_UNREACHABLE:
	case IP_DEST_PORT_UNREACHABLE:
	case IP_BAD_DESTINATION:
		return WinMTRProbeOutcome::destination_unreachable;
	case IP_PACKET_TOO_BIG:
		return WinMTRProbeOutcome::packet_too_big;
	case IP_REQ_TIMED_OUT:
		return WinMTRProbeOutcome::timeout;
	case IP_GENERAL_FAILURE:
		return WinMTRProbeOutcome::local_error;
	default:
		return WinMTRProbeOutcome::icmp_error;
	}
}

[[nodiscard]] bool is_usable_trace_reply(WinMTRProbeOutcome outcome) noexcept
{
	// Every parsed remote ICMP response is a real observation. Only a no-reply
	// timeout or a failure before the network accepted the request is excluded.
	return outcome != WinMTRProbeOutcome::timeout
		&& outcome != WinMTRProbeOutcome::local_error;
}

[[nodiscard]] bool is_terminal_destination_outcome(
	WinMTRProbeOutcome outcome) noexcept
{
	return outcome == WinMTRProbeOutcome::echo_reply
		|| outcome == WinMTRProbeOutcome::destination_unreachable;
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

struct probe_completion_queue;

struct scheduled_probe final : std::enable_shared_from_this<scheduled_probe> {
	winmtr::probe::ProbeToken token;
	pending_probe probe;
	unique_icmp_handle icmp_handle;
	SOCKADDR_INET destination = {};
	WinMTRTraceOptions options;
	std::vector<std::byte> payload;
	unsigned timeout_ms = 0;
	probe_completion_queue* completions = nullptr;
	PTP_WORK work = nullptr;
	winmtr::probe::CompletionKind completion_kind =
		winmtr::probe::CompletionKind::local_error;
	WinMTRProbeOutcome outcome = WinMTRProbeOutcome::none;
	parsed_reply parsed;
	winmtr::probe::MonotonicMilliseconds completed_at = 0;
	bool destination_reply = false;

	~scheduled_probe() noexcept
	{
		if (work != nullptr) {
			WaitForThreadpoolWorkCallbacks(work, FALSE);
			CloseThreadpoolWork(work);
		}
	}
};

struct probe_completion_queue final {
	void push(std::shared_ptr<scheduled_probe> request) noexcept
	{
		{
			std::scoped_lock lock(mutex);
			ready.push_back(std::move(request));
		}
		changed.notify_one();
	}

	[[nodiscard]] std::vector<std::shared_ptr<scheduled_probe>> drain()
	{
		std::vector<std::shared_ptr<scheduled_probe>> result;
		std::scoped_lock lock(mutex);
		result.reserve(ready.size());
		while (!ready.empty()) {
			result.push_back(std::move(ready.front()));
			ready.pop_front();
		}
		return result;
	}

	void wait_until(std::stop_token stop_token,
		std::chrono::steady_clock::time_point deadline)
	{
		std::unique_lock lock(mutex);
		(void)changed.wait_until(lock, stop_token, deadline,
			[this] { return !ready.empty(); });
	}

	void wait()
	{
		std::unique_lock lock(mutex);
		changed.wait(lock, [this] { return !ready.empty(); });
	}

	std::mutex mutex;
	std::condition_variable_any changed;
	std::deque<std::shared_ptr<scheduled_probe>> ready;
};

class probe_thread_pool final {
public:
	probe_thread_pool(unsigned maximum_threads, unsigned minimum_threads)
	{
		InitializeThreadpoolEnvironment(&environment_);
		pool_ = CreateThreadpool(nullptr);
		if (pool_ == nullptr) {
			DestroyThreadpoolEnvironment(&environment_);
			throw std::system_error(static_cast<int>(GetLastError()),
				std::system_category(), "CreateThreadpool");
		}
		SetThreadpoolThreadMaximum(pool_, maximum_threads);
		(void)SetThreadpoolThreadMinimum(pool_, std::min(maximum_threads, minimum_threads));
		SetThreadpoolCallbackPool(&environment_, pool_);
	}

	~probe_thread_pool() noexcept
	{
		DestroyThreadpoolEnvironment(&environment_);
		if (pool_ != nullptr) CloseThreadpool(pool_);
	}

	probe_thread_pool(const probe_thread_pool&) = delete;
	probe_thread_pool& operator=(const probe_thread_pool&) = delete;

	[[nodiscard]] PTP_CALLBACK_ENVIRON environment() noexcept { return &environment_; }

private:
	PTP_POOL pool_ = nullptr;
	TP_CALLBACK_ENVIRON environment_ = {};
};

void CALLBACK probe_work_callback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept
{
	auto* raw = static_cast<scheduled_probe*>(context);
	std::shared_ptr<scheduled_probe> request;
	try {
		request = raw->shared_from_this();
		issue_probe(request->probe, request->icmp_handle.get(), request->destination,
			request->options, request->payload, request->timeout_ms);
		request->completed_at = monotonic_now();
		request->parsed = parse_reply(request->probe, request->destination.si_family);
		request->outcome = classify_probe_outcome(request->parsed.status);
		if (is_usable_trace_reply(request->outcome)
			&& isValidAddress(request->parsed.address)) {
			request->completion_kind = winmtr::probe::CompletionKind::reply;
			request->destination_reply = is_terminal_destination_outcome(request->outcome)
				&& same_network_address(request->parsed.address, request->destination);
		}
		else if (request->probe.issued
			&& request->probe.issue_error == IP_REQ_TIMED_OUT) {
			request->completion_kind = winmtr::probe::CompletionKind::timeout;
			request->outcome = WinMTRProbeOutcome::timeout;
		}
		else {
			request->completion_kind = winmtr::probe::CompletionKind::local_error;
			request->outcome = WinMTRProbeOutcome::local_error;
		}
	}
	catch (...) {
		if (!request) return;
		request->probe.issued = false;
		request->probe.issue_error = ERROR_NOT_ENOUGH_MEMORY;
		request->completion_kind = winmtr::probe::CompletionKind::local_error;
		request->outcome = WinMTRProbeOutcome::local_error;
	}
	if (request->completed_at == 0) request->completed_at = monotonic_now();
	request->completions->push(std::move(request));
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
	struct trace_guard final {
		WinMTRNet* owner;
		std::uint64_t session;
		~trace_guard() noexcept { owner->finishSession(session); }
	} guard{ this, this_session };

	const auto interval_ms = std::max<std::uint64_t>(1,
		static_cast<std::uint64_t>(trace_options.interval_seconds * 1'000.0 + 0.5));
	const auto per_ttl_inflight = static_cast<unsigned>(std::max<std::uint64_t>(1,
		(static_cast<std::uint64_t>(trace_options.timeout_ms) + interval_ms - 1u) / interval_ms));
	const auto logical_limit = static_cast<unsigned>(std::clamp<std::uint64_t>(
		static_cast<std::uint64_t>(trace_options.max_hops) * per_ttl_inflight,
		128u, 4'096u));
	const auto transport_per_ttl = per_ttl_inflight + 1u;
	const auto transport_limit = static_cast<unsigned>(std::clamp<std::uint64_t>(
		static_cast<std::uint64_t>(trace_options.max_hops) * transport_per_ttl,
		128u, 4'096u));

	winmtr::probe::ProbeScheduler scheduler(winmtr::probe::SchedulerConfig{
		.interval_ms = static_cast<winmtr::probe::MonotonicMilliseconds>(interval_ms),
		.timeout_ms = trace_options.timeout_ms,
		.first_ttl = trace_options.start_ttl,
		.last_ttl = trace_options.max_hops,
		.max_inflight = logical_limit,
		.max_inflight_per_ttl = per_ttl_inflight,
		.max_transport_outstanding = transport_limit,
		.max_transport_outstanding_per_ttl = transport_per_ttl,
		.probes_per_ttl = stop_after_unreached_first_round
			? 1u
			: trace_options.cycles,
	});
	probe_completion_queue completions;
	probe_thread_pool worker_pool(transport_limit,
		std::max(4u, std::min(transport_limit,
			trace_options.max_hops * per_ttl_inflight)));
	std::unordered_map<std::uint64_t, std::shared_ptr<scheduled_probe>> requests;
	requests.reserve(transport_limit);

	auto current_epoch = data_epoch.load(std::memory_order_acquire);
	auto now = monotonic_now();
	scheduler.start(this_session, current_epoch, now);
	const auto mandatory_ttl = std::max(trace_options.start_ttl, trace_options.minimum_ttl);
	// The first path discovery is complete but staggered over one interval.  A
	// full initial sweep avoids hiding a destination behind an early run of
	// silent routers; subsequent cycles use the configured unknown tail.
	const auto initial_ceiling = trace_options.max_hops;
	scheduler.set_last_ttl(initial_ceiling, now);

	std::uint64_t random_state = GetTickCount64() ^ (this_session * 0x9e3779b97f4a7c15ull);
	std::uint64_t reported_cycles = 0;
	bool session_reached_destination = false;
	bool session_had_usable_reply = false;
	bool stopping = false;
	bool exploration_cycle = true;
	bool initial_discovery = true;
	unsigned highest_response_ttl = 0;
	unsigned destination_ttl = 0;
	unsigned stable_ceiling = initial_ceiling;
	unsigned shrink_candidate = 0;
	unsigned shrink_confirmations = 0;

	const auto notify_changed = [this] {
		if (options != nullptr) options->notifyTraceDataChanged();
	};
	const auto refresh_completed_cycles = [&] {
		std::uint64_t minimum = std::numeric_limits<std::uint64_t>::max();
		for (unsigned ttl = trace_options.start_ttl; ttl <= scheduler.active_last_ttl(); ++ttl) {
			const auto& counters = scheduler.counters(ttl);
			minimum = std::min(minimum, counters.completed + counters.local_errors
				+ counters.cache_skipped);
		}
		if (minimum != std::numeric_limits<std::uint64_t>::max()
			&& minimum > reported_cycles) {
			reported_cycles = minimum;
			setCompletedCycles(reported_cycles, current_epoch);
			if (exploration_cycle) {
				const auto tail_origin = highest_response_ttl == 0
					? trace_options.start_ttl - 1u
					: highest_response_ttl;
				const auto normal_ceiling = destination_ttl != 0
					? std::max(mandatory_ttl, destination_ttl)
					: std::min(trace_options.max_hops,
						std::max(mandatory_ttl, tail_origin + trace_options.unknown_host_limit));
				if (initial_discovery) {
					stable_ceiling = normal_ceiling;
					initial_discovery = false;
					shrink_candidate = 0;
					shrink_confirmations = 0;
				}
				else if (normal_ceiling < stable_ceiling) {
					if (shrink_candidate == normal_ceiling) {
						++shrink_confirmations;
					}
					else {
						shrink_candidate = normal_ceiling;
						shrink_confirmations = 1;
					}
					if (shrink_confirmations >= WinMTRUtils::PATH_SHRINK_CONFIRMATIONS) {
						stable_ceiling = normal_ceiling;
						shrink_candidate = 0;
						shrink_confirmations = 0;
					}
				}
				else {
					stable_ceiling = normal_ceiling;
					shrink_candidate = 0;
					shrink_confirmations = 0;
				}
				scheduler.set_last_ttl(stable_ceiling, monotonic_now());
				exploration_cycle = false;
			}
			if (reported_cycles % WinMTRUtils::PATH_EXPLORATION_PERIOD == 0
				&& stable_ceiling < trace_options.max_hops) {
				exploration_cycle = true;
				highest_response_ttl = 0;
				destination_ttl = 0;
				const auto frontier_ceiling = std::min(trace_options.max_hops,
					stable_ceiling + WinMTRUtils::PATH_EXPLORATION_FRONTIER_TTLS);
				scheduler.set_last_ttl(frontier_ceiling, monotonic_now());
			}
		}
	};

	while (true) {
		now = monotonic_now();
		const auto observed_epoch = data_epoch.load(std::memory_order_acquire);
		if (!stopping && observed_epoch != current_epoch) {
			current_epoch = observed_epoch;
			reported_cycles = 0;
			session_reached_destination = false;
			session_had_usable_reply = false;
			exploration_cycle = true;
			initial_discovery = true;
			highest_response_ttl = 0;
			destination_ttl = 0;
			stable_ceiling = initial_ceiling;
			shrink_candidate = 0;
			shrink_confirmations = 0;
			scheduler.restart(current_epoch, now);
			scheduler.set_last_ttl(initial_ceiling, now);
		}

		for (auto& request : completions.drain()) {
			const auto found = requests.find(request->token.sequence);
			if (found == requests.end()) continue;
			const auto disposition = scheduler.complete(request->token,
				request->completion_kind, request->completed_at);
			const auto accepted_after_destination = destination_ttl != 0
				&& request->token.ttl > std::max(mandatory_ttl, destination_ttl)
				&& (disposition == winmtr::probe::CompletionDisposition::accepted_reply
					|| disposition == winmtr::probe::CompletionDisposition::accepted_timeout
					|| disposition == winmtr::probe::CompletionDisposition::accepted_local_error
					|| disposition
						== winmtr::probe::CompletionDisposition::late_discarded_after_timeout);
			if (accepted_after_destination) {
				commitPostDestinationCompletion(request->token.ttl, request->token.epoch);
				if (disposition
					== winmtr::probe::CompletionDisposition::late_discarded_after_timeout) {
					commitLateCompletion(request->token.ttl, request->token.epoch);
				}
				notify_changed();
				requests.erase(found);
				continue;
			}
			switch (disposition) {
			case winmtr::probe::CompletionDisposition::accepted_reply: {
				commitReply(request->token.ttl, request->parsed.address,
					request->parsed.round_trip_ms, request->token.sequence,
					request->probe.completed_tick, this_session, request->token.epoch,
					request->outcome, request->parsed.status, request->destination_reply,
					trace_options.resolve_hostnames,
					trace_options.lookup_asn_isp);
				session_had_usable_reply = true;
				highest_response_ttl = std::max(highest_response_ttl, request->token.ttl);
				if (request->destination_reply) {
					session_reached_destination = true;
					destination_ttl = destination_ttl == 0
						? request->token.ttl
						: std::min(destination_ttl, request->token.ttl);
					scheduler.set_last_ttl(std::max(mandatory_ttl, request->token.ttl), now);
				}
				notify_changed();
				break;
			}
			case winmtr::probe::CompletionDisposition::accepted_timeout:
				commitTimeout(request->token.ttl, request->token.epoch);
				notify_changed();
				break;
			case winmtr::probe::CompletionDisposition::accepted_local_error:
				commitLocalError(request->token.ttl, request->token.epoch, true,
					request->probe.issue_error != ERROR_SUCCESS
						? request->probe.issue_error
						: request->parsed.status);
				notify_changed();
				break;
			case winmtr::probe::CompletionDisposition::late_discarded:
				commitLateCompletion(request->token.ttl, request->token.epoch);
				notify_changed();
				break;
			case winmtr::probe::CompletionDisposition::late_discarded_after_timeout:
				commitTimeout(request->token.ttl, request->token.epoch);
				commitLateCompletion(request->token.ttl, request->token.epoch);
				notify_changed();
				break;
			case winmtr::probe::CompletionDisposition::ignored_epoch:
			case winmtr::probe::CompletionDisposition::unknown_token:
				break;
			}
			requests.erase(found);
		}

		if (stop_token.stop_requested() && !stopping) {
			stopping = true;
			scheduler.stop();
		}
		if (!stopping) {
			for (const auto& expired : scheduler.expire(now)) {
				if (destination_ttl != 0
					&& expired.ttl > std::max(mandatory_ttl, destination_ttl)) {
					commitPostDestinationCompletion(expired.ttl, expired.epoch);
				}
				else {
					commitTimeout(expired.ttl, expired.epoch);
				}
				notify_changed();
			}

			auto due = scheduler.reserve_due(now);
			for (const auto ttl : due.skipped_ttls) {
				commitSchedulerSkipped(ttl, current_epoch);
				notify_changed();
			}
			for (const auto& slot : due.slots) {
				bool cached_destination = false;
				if (replyIsCached(slot.token.ttl, GetTickCount64(),
					trace_options.reply_cache_seconds, slot.token.epoch,
					cached_destination)) {
					(void)scheduler.mark_cached(slot.token);
					commitCacheSkipped(slot.token.ttl, slot.token.epoch);
					if (cached_destination) {
						session_reached_destination = true;
						scheduler.set_last_ttl(std::max(mandatory_ttl, slot.token.ttl), now);
					}
					notify_changed();
					continue;
				}
				try {
					auto request = std::make_shared<scheduled_probe>();
					request->token = slot.token;
					request->probe.ttl = slot.token.ttl;
					request->icmp_handle = create_icmp_handle(address.si_family);
					request->destination = address;
					request->options = trace_options;
					request->payload = make_payload(trace_options, random_state);
					request->timeout_ms = trace_options.timeout_ms;
					request->completions = &completions;
					if (!request->icmp_handle) {
						const auto error = GetLastError();
						(void)scheduler.mark_issue_failed(slot.token);
						commitLocalError(slot.token.ttl, slot.token.epoch, false,
							error != ERROR_SUCCESS ? error : ERROR_INVALID_HANDLE);
						notify_changed();
						continue;
					}
					request->work = CreateThreadpoolWork(probe_work_callback,
						request.get(), worker_pool.environment());
					if (request->work == nullptr) {
						const auto error = GetLastError();
						(void)scheduler.mark_issue_failed(slot.token);
						commitLocalError(slot.token.ttl, slot.token.epoch, false,
							error != ERROR_SUCCESS ? error : ERROR_NOT_ENOUGH_MEMORY);
						notify_changed();
						continue;
					}
					requests.emplace(slot.token.sequence, request);
					if (!scheduler.mark_issued(slot.token, now)) {
						requests.erase(slot.token.sequence);
						continue;
					}
					const auto scheduler_lateness_ms = now > slot.scheduled_at
						? static_cast<std::uint64_t>(now - slot.scheduled_at)
						: 0u;
					commitIssued(slot.token.ttl, slot.token.epoch,
						scheduler_lateness_ms);
					notify_changed();
					SubmitThreadpoolWork(request->work);
				}
				catch (...) {
					(void)scheduler.mark_issue_failed(slot.token);
					commitLocalError(slot.token.ttl, slot.token.epoch, false,
						ERROR_NOT_ENOUGH_MEMORY);
					notify_changed();
				}
			}
			refresh_completed_cycles();
			if (scheduler.quotas_reached() && scheduler.logical_inflight() == 0) {
				stopping = true;
				scheduler.stop();
			}
		}

		if (stopping && scheduler.transport_outstanding() == 0 && requests.empty()) break;

		if (stopping) {
			completions.wait();
			continue;
		}
		const auto wake = scheduler.next_wake_at();
		if (!wake || *wake <= now) continue;
		completions.wait_until(stop_token, std::chrono::steady_clock::time_point{
			std::chrono::milliseconds{ *wake } });
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
	session_started_at_unix_ms = unix_now_ms();
	session_ended_at_unix_ms = 0;
	session_started_tick = GetTickCount64();
	session_ended_tick = 0;
	++data_revision;
	for (unsigned index = 0; index < host.size(); ++index) {
		host[index].reset(index + 1);
	}
}

void WinMTRNet::finishSession(std::uint64_t expected_session) noexcept
{
	std::scoped_lock lock(ghMutex);
	if (session_id.load(std::memory_order_relaxed) != expected_session) return;
	session_ended_at_unix_ms = unix_now_ms();
	session_ended_tick = GetTickCount64();
	tracing.store(false, std::memory_order_release);
	++data_revision;
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

void WinMTRNet::setCompletedCycles(std::uint64_t cycles,
	std::uint64_t expected_epoch) noexcept
{
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch
		|| cycles <= completed_cycles) {
		return;
	}
	completed_cycles = cycles;
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

void WinMTRNet::commitIssued(unsigned ttl, std::uint64_t expected_epoch,
	std::uint64_t scheduler_lateness_ms) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].noteIssued(scheduler_lateness_ms);
	display_max_ttl = std::max(display_max_ttl,
		std::clamp(ttl, session_start_ttl, session_options.max_hops));
	++data_revision;
}

void WinMTRNet::commitLocalError(unsigned ttl, std::uint64_t expected_epoch,
	bool was_issued, std::uint32_t error_code) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].noteLocalError(was_issued, error_code);
	++data_revision;
}

void WinMTRNet::commitSchedulerSkipped(unsigned ttl,
	std::uint64_t expected_epoch) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].noteSchedulerSkipped();
	++data_revision;
}

void WinMTRNet::commitCacheSkipped(unsigned ttl,
	std::uint64_t expected_epoch) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].noteCacheSkipped();
	++data_revision;
}

void WinMTRNet::commitLateCompletion(unsigned ttl,
	std::uint64_t expected_epoch) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].noteLateCompletion();
	++data_revision;
}

void WinMTRNet::commitPostDestinationCompletion(unsigned ttl,
	std::uint64_t expected_epoch) noexcept
{
	if (ttl == 0 || ttl > host.size()) return;
	std::scoped_lock lock(ghMutex);
	if (data_epoch.load(std::memory_order_relaxed) != expected_epoch) return;
	host[ttl - 1].notePostDestinationCompletion();
	++data_revision;
}

void WinMTRNet::commitReply(unsigned ttl, const SOCKADDR_INET& responder,
	unsigned round_trip_ms, std::uint64_t cycle, std::uint64_t tick,
	std::uint64_t expected_session, std::uint64_t expected_epoch,
	WinMTRProbeOutcome outcome, std::uint32_t status_code, bool is_destination,
	bool resolve_hostname, bool lookup_asn_isp)
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
		hop.noteReply(round_trip_ms, cycle, tick, outcome, status_code);
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
