#pragma once

// This header is intentionally consumable by both C++ and the Windows resource
// compiler. Keep the preprocessor constants as narrow string literals; the C++
// section below exposes matching compile-time wide string views.

#define WINMTR_BRAND_VERSION_MAJOR 1
#define WINMTR_BRAND_VERSION_MINOR 0
#define WINMTR_BRAND_VERSION_PATCH 0
#define WINMTR_BRAND_VERSION_BUILD 0
#define WINMTR_BRAND_FILE_VERSION 1, 0, 0, 0

#define WINMTR_BRAND_DISPLAY_VERSION "1.00"
#define WINMTR_BRAND_FILE_VERSION_TEXT "1.0.0.0"
#define WINMTR_BRAND_PRODUCT_SHORT_NAME "WinMTR"
#define WINMTR_BRAND_COMPANY_NAME "Hyper Network Technology LTD"
#define WINMTR_BRAND_COMPANY_URL "https://hypernetwork.tw"
#define WINMTR_BRAND_PROJECT_URL \
	"https://github.com/HyperNetworkTech/WinMTR-Hyper"
#define WINMTR_BRAND_PRODUCT_NAME \
	"WinMTR v1.00 (Hyper Network Technology LTD)"
#define WINMTR_BRAND_HTTP_USER_AGENT "WinMTR Hyper/1.00"

#define WINMTR_BRAND_MAIN_WINDOW_TITLE WINMTR_BRAND_PRODUCT_NAME
#ifdef RC_INVOKED
#define WINMTR_BRAND_OPTIONS_WINDOW_TITLE L"\x8FFD\x8E64\x9078\x9805"
#define WINMTR_BRAND_NODE_DETAILS_WINDOW_TITLE \
	L"\x7BC0\x9EDE\x8A73\x7D30\x8CC7\x6599"
#define WINMTR_BRAND_NETWORK_INFO_WINDOW_TITLE \
	L"\x76EE\x524D\x7DB2\x8DEF\x8CC7\x8A0A"
#define WINMTR_BRAND_HELP_WINDOW_TITLE \
	L"WinMTR \x8AAA\x660E"
#define WINMTR_BRAND_LICENSE_WINDOW_TITLE \
	L"\x6388\x6B0A\x8207\x8CA2\x737B\x8005"
#else
#define WINMTR_BRAND_OPTIONS_WINDOW_TITLE "\u8FFD\u8E64\u9078\u9805"
#define WINMTR_BRAND_NODE_DETAILS_WINDOW_TITLE \
	"\u7BC0\u9EDE\u8A73\u7D30\u8CC7\u6599"
#define WINMTR_BRAND_NETWORK_INFO_WINDOW_TITLE \
	"\u76EE\u524D\u7DB2\u8DEF\u8CC7\u8A0A"
#define WINMTR_BRAND_HELP_WINDOW_TITLE \
	"WinMTR \u8AAA\u660E"
#define WINMTR_BRAND_LICENSE_WINDOW_TITLE \
	"\u6388\u6B0A\u8207\u8CA2\u737B\u8005"
#endif

#define WINMTR_BRAND_UI_FONT "Microsoft JhengHei UI"
#define WINMTR_BRAND_TABLE_FONT "Consolas"

#ifdef RC_INVOKED
#define WINMTR_UI_VALUE_UNAVAILABLE L"\x7121\x6CD5\x53D6\x5F97"
#define WINMTR_UI_ECS_SUPPORTED L"\x652F\x63F4\xFF08\x5BE6\x969B\x5B50\x7DB2\x8DEF\xFF09"
#define WINMTR_UI_ECS_UNSUPPORTED L"\x4E0D\x652F\x63F4"
#define WINMTR_UI_ECS_UNKNOWN L"\x7121\x6CD5\x5224\x65B7"
#else
#define WINMTR_UI_VALUE_UNAVAILABLE "\u7121\u6CD5\u53D6\u5F97"
#define WINMTR_UI_ECS_SUPPORTED "\u652F\u63F4\uFF08\u5BE6\u969B\u5B50\u7DB2\u8DEF\uFF09"
#define WINMTR_UI_ECS_UNSUPPORTED "\u4E0D\u652F\u63F4"
#define WINMTR_UI_ECS_UNKNOWN "\u7121\u6CD5\u5224\u65B7"
#endif

#define WINMTR_SOURCE_IPINFO_IPV4_NAME "ipinfo.io"
#define WINMTR_SOURCE_IPINFO_IPV4_URL "https://ipinfo.io/json"
#define WINMTR_SOURCE_IPINFO_IPV6_NAME "v6.ipinfo.io"
#define WINMTR_SOURCE_IPINFO_IPV6_URL "https://v6.ipinfo.io/json"
#define WINMTR_SOURCE_AKAHELP_NAME "whoami.ds.akahelp.net"
#define WINMTR_SOURCE_AKAHELP_HOST "whoami.ds.akahelp.net"
#define WINMTR_SOURCE_IPIFY_IPV4_NAME "api4.ipify.org"
#define WINMTR_SOURCE_IPIFY_IPV4_URL "https://api4.ipify.org"
#define WINMTR_SOURCE_IPIFY_IPV6_NAME "api6.ipify.org"
#define WINMTR_SOURCE_IPIFY_IPV6_URL "https://api6.ipify.org"
#define WINMTR_SOURCE_IPAPI_NAME "ipapi.co"
#define WINMTR_SOURCE_IPAPI_URL_TEMPLATE "https://ipapi.co/%s/json/"
#ifdef RC_INVOKED
#define WINMTR_SOURCE_WINDOWS_NETWORK_NAME \
	L"Windows \x7DB2\x8DEF\x8A2D\x5B9A"
#else
#define WINMTR_SOURCE_WINDOWS_NETWORK_NAME \
	"Windows \u7DB2\u8DEF\u8A2D\u5B9A"
#endif
#ifdef RC_INVOKED
#define WINMTR_SOURCE_TEAM_CYMRU_NAME \
	L"Team Cymru ASN \x67E5\x8A62\x670D\x52D9"
#else
#define WINMTR_SOURCE_TEAM_CYMRU_NAME \
	"Team Cymru ASN \u67E5\u8A62\u670D\u52D9"
#endif
#define WINMTR_SOURCE_TEAM_CYMRU_IPV4_SUFFIX ".origin.asn.cymru.com"
#define WINMTR_SOURCE_TEAM_CYMRU_IPV6_SUFFIX ".origin6.asn.cymru.com"
#define WINMTR_SOURCE_TEAM_CYMRU_ASN_SUFFIX ".asn.cymru.com"
#define WINMTR_SOURCE_TEAM_CYMRU_INFO_URL \
	"https://www.team-cymru.com/ip-asn-mapping"

#ifndef RC_INVOKED

#include <string_view>

#define WINMTR_BRAND_WIDEN_IMPL(value) L##value
#define WINMTR_BRAND_WIDEN(value) WINMTR_BRAND_WIDEN_IMPL(value)

namespace WinMTRBranding {
	inline constexpr int version_major = WINMTR_BRAND_VERSION_MAJOR;
	inline constexpr int version_minor = WINMTR_BRAND_VERSION_MINOR;
	inline constexpr int version_patch = WINMTR_BRAND_VERSION_PATCH;
	inline constexpr int version_build = WINMTR_BRAND_VERSION_BUILD;

	inline constexpr std::wstring_view display_version =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_DISPLAY_VERSION);
	inline constexpr std::wstring_view file_version =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_FILE_VERSION_TEXT);
	inline constexpr std::wstring_view product_short_name =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_PRODUCT_SHORT_NAME);
	inline constexpr std::wstring_view product_name =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_PRODUCT_NAME);
	inline constexpr std::wstring_view company_name =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_COMPANY_NAME);
	inline constexpr std::wstring_view company_url =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_COMPANY_URL);
	inline constexpr std::wstring_view project_url =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_PROJECT_URL);
	inline constexpr std::wstring_view http_user_agent =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_HTTP_USER_AGENT);

	inline constexpr std::wstring_view main_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_MAIN_WINDOW_TITLE);
	inline constexpr std::wstring_view options_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_OPTIONS_WINDOW_TITLE);
	inline constexpr std::wstring_view node_details_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_NODE_DETAILS_WINDOW_TITLE);
	inline constexpr std::wstring_view network_info_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_NETWORK_INFO_WINDOW_TITLE);
	inline constexpr std::wstring_view help_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_HELP_WINDOW_TITLE);
	inline constexpr std::wstring_view license_window_title =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_LICENSE_WINDOW_TITLE);

	inline constexpr std::wstring_view ui_font =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_UI_FONT);
	inline constexpr std::wstring_view table_font =
		WINMTR_BRAND_WIDEN(WINMTR_BRAND_TABLE_FONT);

	namespace sources {
		inline constexpr std::wstring_view ipinfo_ipv4_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPINFO_IPV4_NAME);
		inline constexpr std::wstring_view ipinfo_ipv4_url =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPINFO_IPV4_URL);
		inline constexpr std::wstring_view ipinfo_ipv6_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPINFO_IPV6_NAME);
		inline constexpr std::wstring_view ipinfo_ipv6_url =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPINFO_IPV6_URL);
		inline constexpr std::wstring_view akahelp_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_AKAHELP_NAME);
		inline constexpr std::wstring_view akahelp_host =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_AKAHELP_HOST);
		inline constexpr std::wstring_view ipify_ipv4_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPIFY_IPV4_NAME);
		inline constexpr std::wstring_view ipify_ipv4_url =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPIFY_IPV4_URL);
		inline constexpr std::wstring_view ipify_ipv6_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPIFY_IPV6_NAME);
		inline constexpr std::wstring_view ipify_ipv6_url =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPIFY_IPV6_URL);
		inline constexpr std::wstring_view ipapi_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPAPI_NAME);
		inline constexpr std::wstring_view ipapi_url_template =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_IPAPI_URL_TEMPLATE);
		inline constexpr std::wstring_view windows_network_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_WINDOWS_NETWORK_NAME);
		inline constexpr std::wstring_view team_cymru_name =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_TEAM_CYMRU_NAME);
		inline constexpr std::wstring_view team_cymru_ipv4_suffix =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_TEAM_CYMRU_IPV4_SUFFIX);
		inline constexpr std::wstring_view team_cymru_ipv6_suffix =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_TEAM_CYMRU_IPV6_SUFFIX);
		inline constexpr std::wstring_view team_cymru_asn_suffix =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_TEAM_CYMRU_ASN_SUFFIX);
		inline constexpr std::wstring_view team_cymru_info_url =
			WINMTR_BRAND_WIDEN(WINMTR_SOURCE_TEAM_CYMRU_INFO_URL);
	}

	namespace network_strings {
		inline constexpr std::wstring_view unavailable = WINMTR_BRAND_WIDEN(WINMTR_UI_VALUE_UNAVAILABLE);
		inline constexpr std::wstring_view ipv4_section = L"IPv4 \u9023\u7DDA\u8CC7\u8A0A";
		inline constexpr std::wstring_view ipv6_section = L"IPv6 \u9023\u7DDA\u8CC7\u8A0A";
		inline constexpr std::wstring_view recursive_dns_section = L"\u905E\u8FF4 DNS \u9023\u7DDA\u8CC7\u8A0A";
		inline constexpr std::wstring_view address = L"\u4F4D\u5740";
		inline constexpr std::wstring_view hostname = L"\u4E3B\u6A5F\u540D\u7A31";
		inline constexpr std::wstring_view city = L"\u57CE\u5E02";
		inline constexpr std::wstring_view region = L"\u5730\u5340";
		inline constexpr std::wstring_view country = L"\u570B\u5BB6\uFF0F\u5730\u5340";
		inline constexpr std::wstring_view asn = L"ASN";
		inline constexpr std::wstring_view provider = L"\u7DB2\u8DEF\u696D\u8005";
		inline constexpr std::wstring_view dns_public_address = L"DNS \u5C0D\u5916\u4F4D\u5740";
		inline constexpr std::wstring_view dns_location_provider = L"DNS \u6240\u5728\u5730\u6216\u696D\u8005\u8CC7\u8A0A";
		inline constexpr std::wstring_view ecs = L"EDNS \u7528\u6236\u7AEF\u5B50\u7DB2\u8DEF\uFF08ECS\uFF09";
		inline constexpr std::wstring_view local_dns = L"\u672C\u6A5F DNS \u4F3A\u670D\u5668";
		inline constexpr std::wstring_view sources = L"\u8CC7\u6599\u4F86\u6E90";
		inline constexpr std::wstring_view ecs_supported = WINMTR_BRAND_WIDEN(WINMTR_UI_ECS_SUPPORTED);
		inline constexpr std::wstring_view ecs_unsupported = WINMTR_BRAND_WIDEN(WINMTR_UI_ECS_UNSUPPORTED);
		inline constexpr std::wstring_view ecs_unknown = WINMTR_BRAND_WIDEN(WINMTR_UI_ECS_UNKNOWN);
	}

	namespace cli_strings {
		inline constexpr std::wstring_view unknown_option =
			L"\u7121\u6CD5\u8FA8\u8B58\u547D\u4EE4\u5217\u9078\u9805\uFF1A";
		inline constexpr std::wstring_view missing_value =
			L"\u547D\u4EE4\u5217\u9078\u9805\u7F3A\u5C11\u503C\uFF1A";
		inline constexpr std::wstring_view invalid_value =
			L"\u547D\u4EE4\u5217\u9078\u9805\u7684\u503C\u7121\u6548"
			L"\u6216\u8D85\u51FA\u5141\u8A31\u7BC4\u570D\uFF1A";
		inline constexpr std::wstring_view multiple_targets =
			L"\u547D\u4EE4\u5217\u53EA\u80FD\u6307\u5B9A\u4E00\u500B"
			L"\u76EE\u6A19\u4E3B\u6A5F\u3002";
		inline constexpr std::wstring_view empty_target =
			L"\u547D\u4EE4\u5217\u6307\u5B9A\u7684\u76EE\u6A19\u4E3B"
			L"\u6A5F\u4E0D\u53EF\u70BA\u7A7A\u3002";
		inline constexpr std::wstring_view generic_error =
			L"\u547D\u4EE4\u5217\u53C3\u6578\u7121\u6548\u3002";
	}
}

#endif // !RC_INVOKED
