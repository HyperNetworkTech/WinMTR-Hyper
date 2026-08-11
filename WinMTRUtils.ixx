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

export module WinMTRUtils;
import <string_view>;
using namespace std::literals;
export namespace WinMTRUtils {
	export constexpr auto int_number_format = L"{:Ld}"sv;
	export constexpr auto float_number_format = L"{:.1Lf}"sv;

	// All user-adjustable trace defaults and limits live here so the dialog,
	// command-line parser and trace engine cannot silently drift apart.
	export constexpr auto DEFAULT_PING_SIZE = 64u;
	export constexpr auto MIN_PING_SIZE = 0u;
	export constexpr auto MAX_PING_SIZE = 4096u;
	export constexpr auto DEFAULT_INTERVAL = 1.0;
	export constexpr auto MIN_INTERVAL = 0.1;
	export constexpr auto MAX_INTERVAL = 60.0;
	export constexpr auto DEFAULT_MAX_LRU = 128u;
	export constexpr auto MIN_MAX_LRU = 1u;
	export constexpr auto MAX_MAX_LRU = 256u;

	export constexpr auto DEFAULT_MAX_HOPS = 30u;
	export constexpr auto MIN_MAX_HOPS = 1u;
	export constexpr auto MAX_MAX_HOPS = 64u;

	export constexpr auto DEFAULT_TIMEOUT_MS = 3000u;
	export constexpr auto MIN_TIMEOUT_MS = 100u;
	export constexpr auto MAX_TIMEOUT_MS = 10000u;

	export constexpr auto DEFAULT_CYCLES = 0u;
	export constexpr auto MIN_CYCLES = 0u;
	export constexpr auto MAX_CYCLES = 100000u;
	export constexpr auto DEFAULT_GRACE_MS = 5000u;
	export constexpr auto MIN_GRACE_MS = 0u;
	export constexpr auto MAX_GRACE_MS = 30000u;
	export constexpr auto DEFAULT_MAX_GLOBAL_PPS = 100u;
	export constexpr auto MIN_MAX_GLOBAL_PPS = 1u;
	export constexpr auto MAX_MAX_GLOBAL_PPS = 1000u;

	export constexpr auto DEFAULT_TOS = 0u;
	export constexpr auto MIN_TOS = 0u;
	export constexpr auto MAX_TOS = 255u;

	export constexpr auto DEFAULT_PAYLOAD_PATTERN = 32;
	export constexpr auto MIN_PAYLOAD_PATTERN = -1;
	export constexpr auto MAX_PAYLOAD_PATTERN = 255;

	export constexpr auto DEFAULT_START_TTL = 1u;
	export constexpr auto MIN_START_TTL = 1u;
	export constexpr auto DEFAULT_MINIMUM_TTL = 0u;
	export constexpr auto MIN_MINIMUM_TTL = 0u;

	export constexpr auto DEFAULT_UNKNOWN_HOST_LIMIT = 12u;
	export constexpr auto MIN_UNKNOWN_HOST_LIMIT = 1u;
	export constexpr auto MAX_UNKNOWN_HOST_LIMIT = 64u;

	export constexpr auto DEFAULT_ECMP_DISPLAY_LIMIT = 8u;
	export constexpr auto MIN_ECMP_DISPLAY_LIMIT = 1u;
	export constexpr auto MAX_ECMP_RESPONDERS = 128u;

	export constexpr auto DEFAULT_REPLY_CACHE_SECONDS = 0u;
	export constexpr auto MIN_REPLY_CACHE_SECONDS = 0u;
	export constexpr auto MAX_REPLY_CACHE_SECONDS = 86400u;

	// Country/ASN/ISP/reverse-DNS results are shared across trace sessions.
	// A bounded 24-hour cache prevents repeated external lookups without
	// retaining stale network ownership data indefinitely.
	export constexpr auto RESPONDER_METADATA_CACHE_SECONDS = 86400u;
	export constexpr auto RESPONDER_METADATA_NEGATIVE_CACHE_SECONDS = 600u;
	export constexpr auto MAX_RESPONDER_METADATA_CACHE_ENTRIES = 2048u;
	export constexpr auto METADATA_WORKER_COUNT = 4u;
	export constexpr auto MAX_PENDING_METADATA_JOBS = 1024u;

	export constexpr auto DEFAULT_USE_DNS = true;
	export constexpr auto DEFAULT_LOOKUP_ASN_ISP = true;
	export constexpr auto DEFAULT_DONT_FRAGMENT = true;
	export constexpr auto DEFAULT_USE_IPV4 = true;
	export constexpr auto DEFAULT_USE_IPV6 = true;
	export constexpr auto DEFAULT_QUERY_PUBLIC_NETWORK_INFO = true;
	export constexpr auto PUBLIC_INFO_REFRESH_ON_NETWORK_CHANGE = 0u;
	export constexpr auto PUBLIC_INFO_REFRESH_FIXED_INTERVAL = 1u;
	export constexpr auto DEFAULT_PUBLIC_INFO_REFRESH_MODE = PUBLIC_INFO_REFRESH_ON_NETWORK_CHANGE;
	export constexpr auto DEFAULT_PUBLIC_INFO_REFRESH_MINUTES = 30u;
	export constexpr auto MIN_PUBLIC_INFO_REFRESH_MINUTES = 1u;
	export constexpr auto MAX_PUBLIC_INFO_REFRESH_MINUTES = 1440u;

	// Periodically probe a small frontier beyond the stable path. This detects
	// route growth without generating a burst across every possible TTL.
	export constexpr auto PATH_EXPLORATION_PERIOD = 10u;
	export constexpr auto PATH_EXPLORATION_FRONTIER_TTLS = 5u;
	export constexpr auto PATH_SHRINK_CONFIRMATIONS = 2u;
}
