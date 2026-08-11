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

	export constexpr auto DEFAULT_UNKNOWN_HOST_LIMIT = 5u;
	export constexpr auto MIN_UNKNOWN_HOST_LIMIT = 1u;
	export constexpr auto MAX_UNKNOWN_HOST_LIMIT = 64u;

	export constexpr auto DEFAULT_ECMP_DISPLAY_LIMIT = 8u;
	export constexpr auto MIN_ECMP_DISPLAY_LIMIT = 1u;
	export constexpr auto MAX_ECMP_RESPONDERS = 128u;

	export constexpr auto DEFAULT_REPLY_CACHE_SECONDS = 0u;
	export constexpr auto MIN_REPLY_CACHE_SECONDS = 0u;
	export constexpr auto MAX_REPLY_CACHE_SECONDS = 86400u;

	export constexpr auto DEFAULT_USE_DNS = true;
	export constexpr auto DEFAULT_LOOKUP_ASN_ISP = true;
	export constexpr auto DEFAULT_DONT_FRAGMENT = true;
	export constexpr auto DEFAULT_USE_IPV4 = true;
	export constexpr auto DEFAULT_USE_IPV6 = true;
	export constexpr auto DEFAULT_QUERY_PUBLIC_NETWORK_INFO = true;

	// A periodic full-path discovery round lets a route grow again after a
	// shorter/unknown tail was observed.  It is deliberately not user-facing.
	export constexpr auto PATH_EXPLORATION_PERIOD = 10u;
}
