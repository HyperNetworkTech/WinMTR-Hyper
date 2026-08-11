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
module WinMTR.Net:Getters;

import <algorithm>;
import <mutex>;
import <vector>;
import WinMTRSNetHost;
import :ClassDef;

[[nodiscard]]
std::vector<s_nethost> WinMTRNet::getCurrentState() const
{
	std::scoped_lock lock(ghMutex);
	const auto visible_max_ttl = session_destination_ttl == 0
		? display_max_ttl
		: std::min(display_max_ttl, session_destination_ttl);
	const auto count = std::min<std::size_t>(visible_max_ttl, host.size());
	return std::vector<s_nethost>(host.begin(), host.begin() + count);
}

[[nodiscard]]
WinMTRTraceSnapshot WinMTRNet::getTraceSnapshot() const
{
	std::scoped_lock lock(ghMutex);
	WinMTRTraceSnapshot snapshot;
	snapshot.session_id = session_id.load(std::memory_order_relaxed);
	snapshot.data_epoch = data_epoch.load(std::memory_order_relaxed);
	snapshot.revision = data_revision;
	snapshot.started_at_unix_ms = session_started_at_unix_ms;
	snapshot.ended_at_unix_ms = session_ended_at_unix_ms;
	snapshot.target = target_name;
	snapshot.target_address = last_remote_addr;
	snapshot.address_family = last_remote_addr.si_family;
	snapshot.start_ttl = session_start_ttl;
	const auto visible_max_ttl = session_destination_ttl == 0
		? display_max_ttl
		: std::min(display_max_ttl, session_destination_ttl);
	snapshot.display_max_ttl = visible_max_ttl;
	snapshot.tracing = tracing.load(std::memory_order_relaxed);
	const auto duration_end = snapshot.tracing
		? GetTickCount64()
		: session_ended_tick;
	if (session_started_tick != 0 && duration_end >= session_started_tick) {
		snapshot.duration_ms = duration_end - session_started_tick;
	}

	if (visible_max_ttl < session_start_ttl || visible_max_ttl == 0) {
		return snapshot;
	}
	const auto first_index = static_cast<std::size_t>(session_start_ttl - 1);
	auto end_index = std::min<std::size_t>(visible_max_ttl, host.size());
	snapshot.hops.assign(host.begin() + first_index, host.begin() + end_index);
	for (const auto& hop : snapshot.hops) {
		if (hop.xmit != 0) {
			snapshot.first_actual_ttl = hop.hop;
			snapshot.first_actual_sent = hop.xmit;
			break;
		}
	}
	return snapshot;
}

[[nodiscard]]
int WinMTRNet::GetMax() const
{
	std::scoped_lock lock(ghMutex);
	return static_cast<int>(session_destination_ttl == 0
		? display_max_ttl
		: std::min(display_max_ttl, session_destination_ttl));
}
