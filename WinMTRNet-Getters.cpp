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
	const auto count = std::min<std::size_t>(display_max_ttl, host.size());
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
	snapshot.target = target_name;
	snapshot.target_address = last_remote_addr;
	snapshot.address_family = last_remote_addr.si_family;
	snapshot.start_ttl = session_start_ttl;
	snapshot.display_max_ttl = display_max_ttl;
	snapshot.tracing = tracing.load(std::memory_order_relaxed);

	if (display_max_ttl < session_start_ttl || display_max_ttl == 0) {
		return snapshot;
	}
	const auto first_index = static_cast<std::size_t>(session_start_ttl - 1);
	auto end_index = std::min<std::size_t>(display_max_ttl, host.size());
	if (completed_cycles == 0) {
		// Probes in a round run concurrently. Publish only the completed prefix
		// during the first round so rows appear hop-by-hop without temporarily
		// labelling an unfinished lower TTL as an unknown host.
		for (size_t index = first_index; index < end_index; ++index) {
			if (host[index].xmit == 0) {
				end_index = index;
				break;
			}
		}
	}
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
	return static_cast<int>(display_max_ttl);
}
