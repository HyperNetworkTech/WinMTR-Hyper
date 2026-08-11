/*
WinMTR
Copyright (C)  2010-2019 Appnor MSP S.A. - http://www.appnor.com
Copyright (C) 2019-2021 Leetsoftwerx

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

export module WinMTROptionsProvider;

import <algorithm>;
import <cmath>;
import WinMTRUtils;

export struct WinMTRTraceOptions final {
	double interval_seconds = WinMTRUtils::DEFAULT_INTERVAL;
	unsigned packet_size = WinMTRUtils::DEFAULT_PING_SIZE;
	unsigned max_hops = WinMTRUtils::DEFAULT_MAX_HOPS;
	unsigned timeout_ms = WinMTRUtils::DEFAULT_TIMEOUT_MS;
	unsigned cycles = WinMTRUtils::DEFAULT_CYCLES;
	unsigned grace_ms = WinMTRUtils::DEFAULT_GRACE_MS;
	unsigned max_global_pps = WinMTRUtils::DEFAULT_MAX_GLOBAL_PPS;
	unsigned tos = WinMTRUtils::DEFAULT_TOS;
	int payload_pattern = WinMTRUtils::DEFAULT_PAYLOAD_PATTERN;
	unsigned start_ttl = WinMTRUtils::DEFAULT_START_TTL;
	unsigned minimum_ttl = WinMTRUtils::DEFAULT_MINIMUM_TTL;
	unsigned unknown_host_limit = WinMTRUtils::DEFAULT_UNKNOWN_HOST_LIMIT;
	unsigned ecmp_display_limit = WinMTRUtils::DEFAULT_ECMP_DISPLAY_LIMIT;
	unsigned reply_cache_seconds = WinMTRUtils::DEFAULT_REPLY_CACHE_SECONDS;
	bool resolve_hostnames = WinMTRUtils::DEFAULT_USE_DNS;
	bool lookup_asn_isp = WinMTRUtils::DEFAULT_LOOKUP_ASN_ISP;
	bool dont_fragment = WinMTRUtils::DEFAULT_DONT_FRAGMENT;
	bool use_ipv4 = WinMTRUtils::DEFAULT_USE_IPV4;
	bool use_ipv6 = WinMTRUtils::DEFAULT_USE_IPV6;
	bool query_public_network_info = WinMTRUtils::DEFAULT_QUERY_PUBLIC_NETWORK_INFO;
};

/***
* Note: Implementers must ensure that calling any of the methods is thread safe
*/
export struct __declspec(novtable) IWinMTROptionsProvider {
	virtual unsigned getPingSize() const noexcept = 0;
	virtual double getInterval() const noexcept = 0;
	virtual bool getUseDNS() const noexcept = 0;

	// These defaults keep older providers source-compatible while the UI and
	// command-line layers migrate to the expanded option set.
	virtual unsigned getMaxHops() const noexcept { return WinMTRUtils::DEFAULT_MAX_HOPS; }
	virtual unsigned getTimeoutMs() const noexcept { return WinMTRUtils::DEFAULT_TIMEOUT_MS; }
	virtual unsigned getCycles() const noexcept { return WinMTRUtils::DEFAULT_CYCLES; }
	virtual unsigned getGraceMs() const noexcept { return WinMTRUtils::DEFAULT_GRACE_MS; }
	virtual unsigned getMaxGlobalPps() const noexcept { return WinMTRUtils::DEFAULT_MAX_GLOBAL_PPS; }
	virtual unsigned getTos() const noexcept { return WinMTRUtils::DEFAULT_TOS; }
	virtual int getPayloadPattern() const noexcept { return WinMTRUtils::DEFAULT_PAYLOAD_PATTERN; }
	virtual unsigned getStartTtl() const noexcept { return WinMTRUtils::DEFAULT_START_TTL; }
	virtual unsigned getMinimumTtl() const noexcept { return WinMTRUtils::DEFAULT_MINIMUM_TTL; }
	virtual unsigned getUnknownHostLimit() const noexcept { return WinMTRUtils::DEFAULT_UNKNOWN_HOST_LIMIT; }
	virtual unsigned getEcmpDisplayLimit() const noexcept { return WinMTRUtils::DEFAULT_ECMP_DISPLAY_LIMIT; }
	virtual unsigned getReplyCacheSeconds() const noexcept { return WinMTRUtils::DEFAULT_REPLY_CACHE_SECONDS; }
	virtual bool getDontFragment() const noexcept { return WinMTRUtils::DEFAULT_DONT_FRAGMENT; }
	virtual bool getLookupAsnIsp() const noexcept { return WinMTRUtils::DEFAULT_LOOKUP_ASN_ISP; }
	virtual bool getUseIPv4() const noexcept { return WinMTRUtils::DEFAULT_USE_IPV4; }
	virtual bool getUseIPv6() const noexcept { return WinMTRUtils::DEFAULT_USE_IPV6; }
	virtual bool getQueryPublicNetworkInfo() const noexcept { return WinMTRUtils::DEFAULT_QUERY_PUBLIC_NETWORK_INFO; }
	// Trace workers call this after each completed probe so UI providers can
	// schedule a redraw without polling for the end of the whole TTL batch.
	virtual void notifyTraceDataChanged() const noexcept {}

	[[nodiscard]]
	WinMTRTraceOptions snapshotTraceOptions() const noexcept
	{
		WinMTRTraceOptions value;
		const auto requested_interval = getInterval();
		value.interval_seconds = std::isfinite(requested_interval)
			? std::clamp(requested_interval, WinMTRUtils::MIN_INTERVAL, WinMTRUtils::MAX_INTERVAL)
			: WinMTRUtils::DEFAULT_INTERVAL;
		value.packet_size = std::clamp(getPingSize(), WinMTRUtils::MIN_PING_SIZE, WinMTRUtils::MAX_PING_SIZE);
		value.max_hops = std::clamp(getMaxHops(), WinMTRUtils::MIN_MAX_HOPS, WinMTRUtils::MAX_MAX_HOPS);
		value.timeout_ms = std::clamp(getTimeoutMs(), WinMTRUtils::MIN_TIMEOUT_MS, WinMTRUtils::MAX_TIMEOUT_MS);
		value.cycles = std::clamp(getCycles(), WinMTRUtils::MIN_CYCLES, WinMTRUtils::MAX_CYCLES);
		value.grace_ms = std::clamp(getGraceMs(), WinMTRUtils::MIN_GRACE_MS,
			WinMTRUtils::MAX_GRACE_MS);
		value.max_global_pps = std::clamp(getMaxGlobalPps(),
			WinMTRUtils::MIN_MAX_GLOBAL_PPS, WinMTRUtils::MAX_MAX_GLOBAL_PPS);
		value.tos = std::clamp(getTos(), WinMTRUtils::MIN_TOS, WinMTRUtils::MAX_TOS);
		value.payload_pattern = std::clamp(getPayloadPattern(), WinMTRUtils::MIN_PAYLOAD_PATTERN, WinMTRUtils::MAX_PAYLOAD_PATTERN);
		value.start_ttl = std::clamp(getStartTtl(), WinMTRUtils::MIN_START_TTL, value.max_hops);
		value.minimum_ttl = std::clamp(getMinimumTtl(), WinMTRUtils::MIN_MINIMUM_TTL, value.max_hops);
		value.unknown_host_limit = std::clamp(getUnknownHostLimit(), WinMTRUtils::MIN_UNKNOWN_HOST_LIMIT, WinMTRUtils::MAX_UNKNOWN_HOST_LIMIT);
		value.ecmp_display_limit = std::clamp(getEcmpDisplayLimit(), WinMTRUtils::MIN_ECMP_DISPLAY_LIMIT, WinMTRUtils::MAX_ECMP_RESPONDERS);
		value.reply_cache_seconds = std::clamp(getReplyCacheSeconds(), WinMTRUtils::MIN_REPLY_CACHE_SECONDS, WinMTRUtils::MAX_REPLY_CACHE_SECONDS);
		value.resolve_hostnames = getUseDNS();
		value.lookup_asn_isp = getLookupAsnIsp();
		value.dont_fragment = getDontFragment();
		value.use_ipv4 = getUseIPv4();
		value.use_ipv6 = getUseIPv6();
		value.query_public_network_info = getQueryPublicNetworkInfo();
		return value;
	}
};

