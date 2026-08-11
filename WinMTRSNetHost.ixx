/*
WinMTR
Copyright (C)  2010-2019 Appnor MSP S.A. - http://www.appnor.com
Copyright (C) 2019-2022 Leetsoftwerx

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
#include <WinSock2.h>
#include <ws2ipdef.h>
export module WinMTRSNetHost;

import WinMTRIPUtils;
import WinMTRUtils;
import <algorithm>;
import <string>;
import <vector>;
import <cstddef>;
import <cstdint>;
import <cmath>;
import <cstring>;
import <utility>;

export [[nodiscard]]
inline bool same_network_address(const SOCKADDR_INET& lhs, const SOCKADDR_INET& rhs) noexcept
{
	if (lhs.si_family != rhs.si_family) {
		return false;
	}
	if (lhs.si_family == AF_INET) {
		return lhs.Ipv4.sin_addr.S_un.S_addr == rhs.Ipv4.sin_addr.S_un.S_addr;
	}
	if (lhs.si_family == AF_INET6) {
		return lhs.Ipv6.sin6_scope_id == rhs.Ipv6.sin6_scope_id
			&& std::memcmp(&lhs.Ipv6.sin6_addr, &rhs.Ipv6.sin6_addr, sizeof(IN6_ADDR)) == 0;
	}
	return false;
}

export struct s_netresponder final {
	SOCKADDR_INET addr = {};
	std::uint64_t stable_id = 0;
	std::wstring name;
	std::wstring country;
	std::wstring asn;
	std::wstring isp;
	std::uint64_t last_seen_sequence = 0;
	std::uint64_t last_reply_tick = 0;
	std::uint64_t hit_count = 0;

	[[nodiscard]]
	std::wstring getName() const
	{
		return name.empty() ? addr_to_string(addr) : name;
	}
};

export enum class WinMTRProbeOutcome : std::uint8_t {
	none,
	in_flight,
	echo_reply,
	ttl_expired,
	destination_unreachable,
	packet_too_big,
	icmp_error,
	timeout,
	local_error,
	scheduler_skipped,
	cached,
	late_discarded,
	post_destination_discarded,
};

export struct s_nethost final {
	SOCKADDR_INET addr = {};
	std::wstring name;
	std::wstring country;
	std::wstring asn;
	std::wstring isp;
	std::uint64_t xmit = 0;		// probes accepted by the transport (Sent column)
	std::uint64_t completed = 0;	// received + timed_out; excludes in-flight/local failures
	std::uint64_t returned = 0;	// completed probes with a usable ICMP reply
	std::uint64_t timed_out = 0;	// logical network deadlines reached
	std::uint64_t in_flight = 0;	// issued but not logically completed
	std::uint64_t local_errors = 0;
	std::uint64_t scheduler_skipped = 0;
	std::uint64_t cache_skipped = 0;
	std::uint64_t late_completions = 0;
	std::uint64_t post_destination_completions = 0;
	std::uint64_t scheduler_late_slots = 0;
	std::uint64_t scheduler_lateness_total_ms = 0;
	std::uint64_t scheduler_lateness_max_ms = 0;
	std::uint64_t total = 0;		// total round-trip time in milliseconds
	int last = 0;				// last time
	int best = 0;				// best time
	int worst = 0;			// worst time
	double jitter = 0.0;
	double stddev = 0.0;
	unsigned hop = 0;
	std::uint64_t last_reply_tick = 0;
	std::uint64_t last_reply_cycle = 0;
	std::uint64_t last_destination_reply_tick = 0;
	std::vector<s_netresponder> responders;

	// Welford state is intentionally carried in snapshots as harmless data;
	// exporters should consume getAverageMs()/stddev rather than these fields.
	double mean_ms = 0.0;
	double m2_ms = 0.0;
	double previous_reply_ms = 0.0;
	double recent_jitter_ms = 0.0;
	bool has_previous_reply = false;
	WinMTRProbeOutcome last_outcome = WinMTRProbeOutcome::none;
	std::uint32_t last_error_code = 0;

	[[nodiscard]]
	inline int getPercent() const noexcept {
		if (completed == 0) {
			return 0;
		}
		return static_cast<int>(std::lround(100.0 * static_cast<double>(timed_out)
			/ static_cast<double>(completed)));
	}
	[[nodiscard]]
	inline int getAvg() const noexcept {
		return returned == 0 ? 0 : static_cast<int>(std::lround(mean_ms));
	}
	[[nodiscard]]
	inline double getLossPercent() const noexcept {
		return completed == 0 ? 0.0 : 100.0 * static_cast<double>(timed_out)
			/ static_cast<double>(completed);
	}
	[[nodiscard]]
	inline double getAverageMs() const noexcept {
		return returned == 0 ? 0.0 : mean_ms;
	}
	[[nodiscard]]
	auto getName() const -> std::wstring {
		if (name.empty()) {
			return addr_to_string(addr);
		}
		return name;
	}

	void reset(unsigned hop_number = 0) noexcept
	{
		*this = s_nethost{};
		hop = hop_number;
	}

	void noteIssued(std::uint64_t scheduler_lateness_ms) noexcept
	{
		++xmit;
		++in_flight;
		last_outcome = WinMTRProbeOutcome::in_flight;
		last_error_code = 0;
		if (scheduler_lateness_ms != 0) {
			++scheduler_late_slots;
			scheduler_lateness_total_ms += scheduler_lateness_ms;
			scheduler_lateness_max_ms = std::max(scheduler_lateness_max_ms,
				scheduler_lateness_ms);
		}
	}

	void noteTimeout() noexcept
	{
		++completed;
		++timed_out;
		if (in_flight != 0) --in_flight;
		last_outcome = WinMTRProbeOutcome::timeout;
		last_error_code = 0;
	}

	void noteReply(unsigned round_trip_ms, std::uint64_t cycle, std::uint64_t tick,
		WinMTRProbeOutcome outcome, std::uint32_t status_code) noexcept
	{
		++completed;
		++returned;
		if (in_flight != 0) --in_flight;
		last = static_cast<int>(round_trip_ms);
		total += round_trip_ms;
		if (returned == 1 || last < best) {
			best = last;
		}
		if (returned == 1 || last > worst) {
			worst = last;
		}

		const auto sample = static_cast<double>(round_trip_ms);
		const auto delta = sample - mean_ms;
		mean_ms += delta / static_cast<double>(returned);
		const auto delta_after_mean = sample - mean_ms;
		m2_ms += delta * delta_after_mean;
		stddev = returned > 1
			? std::sqrt(m2_ms / static_cast<double>(returned - 1u))
			: 0.0;

		if (has_previous_reply) {
			recent_jitter_ms = std::abs(sample - previous_reply_ms);
			jitter += (recent_jitter_ms - jitter) / 16.0;
		}
		previous_reply_ms = sample;
		has_previous_reply = true;
		last_reply_tick = tick;
		last_reply_cycle = cycle;
		last_outcome = outcome;
		last_error_code = status_code;
	}

	void noteLocalError(bool was_issued, std::uint32_t error_code) noexcept
	{
		++local_errors;
		if (was_issued && in_flight != 0) --in_flight;
		last_outcome = WinMTRProbeOutcome::local_error;
		last_error_code = error_code;
	}

	void noteSchedulerSkipped() noexcept
	{
		++scheduler_skipped;
		last_outcome = WinMTRProbeOutcome::scheduler_skipped;
		last_error_code = 0;
	}

	void noteCacheSkipped() noexcept
	{
		++cache_skipped;
		last_outcome = WinMTRProbeOutcome::cached;
		last_error_code = 0;
	}

	void noteLateCompletion() noexcept
	{
		++late_completions;
		last_outcome = WinMTRProbeOutcome::late_discarded;
	}

	void notePostDestinationCompletion() noexcept
	{
		if (in_flight != 0) --in_flight;
		++post_destination_completions;
		last_outcome = WinMTRProbeOutcome::post_destination_discarded;
		last_error_code = 0;
	}

	[[nodiscard]]
	s_netresponder& observeResponder(const SOCKADDR_INET& responder_address,
		std::uint64_t sequence, std::uint64_t tick)
	{
		auto found = responders.begin();
		for (; found != responders.end(); ++found) {
			if (same_network_address(found->addr, responder_address)) {
				break;
			}
		}

		if (found == responders.end()) {
			if (responders.size() >= WinMTRUtils::MAX_ECMP_RESPONDERS) {
				const auto oldest = std::min_element(responders.begin(), responders.end(),
					[](const auto& lhs, const auto& rhs) {
						return lhs.last_seen_sequence < rhs.last_seen_sequence;
					});
				responders.erase(oldest);
			}
			responders.insert(responders.begin(), s_netresponder{
				.addr = responder_address,
				.stable_id = stableResponderId(responder_address),
			});
		}
		else if (found != responders.begin()) {
			auto current = std::move(*found);
			responders.erase(found);
			responders.insert(responders.begin(), std::move(current));
		}

		auto& primary = responders.front();
		primary.last_seen_sequence = sequence;
		primary.last_reply_tick = tick;
		++primary.hit_count;
		addr = primary.addr;
		name = primary.name;
		country = primary.country;
		asn = primary.asn;
		isp = primary.isp;
		return primary;
	}

private:
	[[nodiscard]] static std::uint64_t stableResponderId(
		const SOCKADDR_INET& address) noexcept
	{
		constexpr std::uint64_t offset = 14695981039346656037ull;
		constexpr std::uint64_t prime = 1099511628211ull;
		std::uint64_t hash = offset;
		const auto mix = [&hash](const void* data, std::size_t size) noexcept {
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (std::size_t index = 0; index < size; ++index) {
				hash ^= bytes[index];
				hash *= prime;
			}
		};
		mix(&address.si_family, sizeof(address.si_family));
		if (address.si_family == AF_INET) {
			mix(&address.Ipv4.sin_addr, sizeof(address.Ipv4.sin_addr));
		}
		else if (address.si_family == AF_INET6) {
			mix(&address.Ipv6.sin6_addr, sizeof(address.Ipv6.sin6_addr));
			mix(&address.Ipv6.sin6_scope_id, sizeof(address.Ipv6.sin6_scope_id));
		}
		return hash == 0 ? 1 : hash;
	}

public:

	bool updateResponder(const SOCKADDR_INET& responder_address,
		const std::wstring& responder_name,
		const std::wstring& responder_country = {},
		const std::wstring& responder_asn = {},
		const std::wstring& responder_isp = {})
	{
		for (auto& responder : responders) {
			if (!same_network_address(responder.addr, responder_address)) {
				continue;
			}
			if (!responder_name.empty()) {
				responder.name = responder_name;
			}
			if (!responder_country.empty()) {
				responder.country = responder_country;
			}
			if (!responder_asn.empty()) {
				responder.asn = responder_asn;
			}
			if (!responder_isp.empty()) {
				responder.isp = responder_isp;
			}
			if (&responder == &responders.front()) {
				name = responder.name;
				country = responder.country;
				asn = responder.asn;
				isp = responder.isp;
			}
			return true;
		}
		return false;
	}
};
