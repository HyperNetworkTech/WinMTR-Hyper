/*
WinMTR
Copyright (C)  2010-2019 Appnor MSP S.A. - http://www.appnor.com
Copyright (C) 2019-2023 Leetsoftwerx

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
#include <winsock2.h>
#include <ws2ipdef.h>
export module WinMTR.Net:ClassDef;

import <optional>;
import <atomic>;
import <array>;
import <mutex>;
import <memory>;
import <stop_token>;
import <vector>;
import <string>;
import <string_view>;
import <cstdint>;
import <unordered_map>;
import <unordered_set>;
import WinMTRSNetHost;
import WinMTROptionsProvider;
import WinMTRUtils;
import winmtr.helper;

export struct WinMTRTraceSnapshot final {
	std::uint64_t session_id = 0;
	std::uint64_t data_epoch = 0;
	std::uint64_t revision = 0;
	std::uint64_t started_at_unix_ms = 0;
	std::uint64_t ended_at_unix_ms = 0;
	std::uint64_t duration_ms = 0;
	std::wstring target;
	SOCKADDR_INET target_address = {};
	ADDRESS_FAMILY address_family = AF_UNSPEC;
	unsigned start_ttl = WinMTRUtils::DEFAULT_START_TTL;
	unsigned display_max_ttl = 0;
	unsigned first_actual_ttl = 0;
	std::uint64_t first_actual_sent = 0;
	bool tracing = false;
	std::vector<s_nethost> hops;
};

export enum class WinMTRTraceResult {
	no_reply,
	reply,
	destination
};

//*****************************************************************************
// CLASS:  WinMTRNet
//
//
//*****************************************************************************
export class WinMTRNet final : public std::enable_shared_from_this<WinMTRNet> {
	WinMTRNet(const WinMTRNet&) = delete;
	WinMTRNet& operator=(const WinMTRNet&) = delete;
public:

	WinMTRNet(const IWinMTROptionsProvider* wp)
		:host(),
		last_remote_addr(),
		options(wp),
		wsaHelper(MAKEWORD(2, 2)),
		tracing() {
		for (unsigned index = 0; index < host.size(); ++index) {
			host[index].hop = index + 1;
		}

		if (!wsaHelper) [[unlikely]] {
			//AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
			return;
		}
	}
	~WinMTRNet() noexcept = default;


	// Synchronous trace scheduler.  The dialog owns the background std::jthread
	// and calls this method from that worker, so no Windows Runtime is required.
	[[nodiscard]] WinMTRTraceResult DoTrace(std::stop_token stop_token, SOCKADDR_INET address,
		std::wstring target = {}, bool stop_after_unreached_first_round = false);

	void ResetHops() noexcept;
	[[nodiscard]]
	int GetMax() const;

	[[nodiscard]]
	std::vector<s_nethost> getCurrentState() const;
	[[nodiscard]]
	WinMTRTraceSnapshot getTraceSnapshot() const;
	s_nethost getStateAt(int at) const
	{
		std::scoped_lock lock(ghMutex);
		if (at < 0 || static_cast<std::size_t>(at) >= host.size()) {
			return {};
		}
		return host[at];
	}
	[[nodiscard]]
	bool isTracing() const noexcept { return tracing.load(std::memory_order_acquire); }
	[[nodiscard]]
	std::uint64_t getSessionId() const noexcept { return session_id.load(std::memory_order_acquire); }
	[[nodiscard]]
	std::uint64_t getDataEpoch() const noexcept { return data_epoch.load(std::memory_order_acquire); }

	// Safe integration point for asynchronous country/ASN/ISP resolvers.
	// Results from an older session/reset are rejected.
	bool updateResponderMetadata(std::uint64_t expected_session,
		std::uint64_t expected_epoch,
		const SOCKADDR_INET& address,
		std::wstring name,
		std::wstring country,
		std::wstring asn,
		std::wstring isp);

	static constexpr auto MAX_HOPS = WinMTRUtils::MAX_MAX_HOPS;
private:
	std::array<s_nethost, WinMTRNet::MAX_HOPS>	host;
	SOCKADDR_INET last_remote_addr;
	std::wstring target_name;
	mutable std::mutex ghMutex;
	const IWinMTROptionsProvider* options;
	winmtr::helper::WSAHelper wsaHelper;
	std::atomic_bool tracing = false;
	std::atomic_uint64_t session_id = 0;
	std::atomic_uint64_t data_epoch = 0;
	std::uint64_t reply_sequence = 0;
	std::uint64_t completed_cycles = 0;
	std::uint64_t data_revision = 0;
	std::uint64_t session_started_at_unix_ms = 0;
	std::uint64_t session_ended_at_unix_ms = 0;
	std::uint64_t session_started_tick = 0;
	std::uint64_t session_ended_tick = 0;
	unsigned session_start_ttl = WinMTRUtils::DEFAULT_START_TTL;
	unsigned display_max_ttl = 0;
	WinMTRTraceOptions session_options;
	struct responder_metadata final {
		std::wstring name;
		std::wstring country;
		std::wstring asn;
		std::wstring isp;
		bool hostname_queried = false;
		bool network_queried = false;
		std::uint64_t cached_at_tick = 0;
	};
	std::unordered_map<std::wstring, responder_metadata> responder_lookup_cache;
	std::unordered_set<std::wstring> reverse_dns_inflight;

	void beginSession(const SOCKADDR_INET& address, std::wstring target,
		const WinMTRTraceOptions& trace_options);
	void finishSession(std::uint64_t expected_session) noexcept;
	void setDisplayMaximum(unsigned ttl, std::uint64_t expected_epoch) noexcept;
	void setCompletedCycles(std::uint64_t cycles, std::uint64_t expected_epoch) noexcept;
	[[nodiscard]] bool replyIsCached(unsigned ttl, std::uint64_t now_tick,
		unsigned cache_seconds, std::uint64_t expected_epoch,
		bool& is_destination) const noexcept;
	void commitTimeout(unsigned ttl, std::uint64_t expected_epoch) noexcept;
	void commitIssued(unsigned ttl, std::uint64_t expected_epoch,
		std::uint64_t scheduler_lateness_ms) noexcept;
	void commitLocalError(unsigned ttl, std::uint64_t expected_epoch,
		bool was_issued, std::uint32_t error_code) noexcept;
	void commitSchedulerSkipped(unsigned ttl, std::uint64_t expected_epoch) noexcept;
	void commitCacheSkipped(unsigned ttl, std::uint64_t expected_epoch) noexcept;
	void commitLateCompletion(unsigned ttl, std::uint64_t expected_epoch) noexcept;
	void commitPostDestinationCompletion(unsigned ttl,
		std::uint64_t expected_epoch) noexcept;
	void commitReply(unsigned ttl, const SOCKADDR_INET& responder, unsigned round_trip_ms,
		std::uint64_t cycle, std::uint64_t tick, std::uint64_t expected_session,
		std::uint64_t expected_epoch, WinMTRProbeOutcome outcome,
		std::uint32_t status_code, bool is_destination, bool resolve_hostname,
		bool lookup_asn_isp);
	void scheduleReverseLookup(const SOCKADDR_INET& address,
		std::uint64_t expected_session, std::uint64_t expected_epoch,
		bool resolve_hostname, bool lookup_asn_isp);
};
