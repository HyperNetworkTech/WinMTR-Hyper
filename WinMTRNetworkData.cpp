#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <WinDNS.h>
#include <WinHTTP.h>
#include <Iphlpapi.h>

#include "WinMTRNetworkData.h"
#include "WinMTRBranding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dnsapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace winmtr::network_data {
namespace {

struct InternetCloser final {
	void operator()(void* value) const noexcept
	{
		if (value != nullptr) {
			WinHttpCloseHandle(value);
		}
	}
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

struct DnsRecordCloser final {
	void operator()(DNS_RECORDW* value) const noexcept
	{
		if (value != nullptr) {
			DnsRecordListFree(value, DnsFreeRecordListDeep);
		}
	}
};
using DnsRecordList = std::unique_ptr<DNS_RECORDW, DnsRecordCloser>;

[[nodiscard]] std::wstring utf8ToWide(std::string_view value)
{
	if (value.empty()) {
		return {};
	}
	const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (required <= 0) {
		return {};
	}
	std::wstring result(static_cast<size_t>(required), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
		result.data(), required);
	return result;
}

[[nodiscard]] std::string wideToUtf8(std::wstring_view value)
{
	if (value.empty()) {
		return {};
	}
	const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		nullptr, 0, nullptr, nullptr);
	if (required <= 0) {
		return {};
	}
	std::string result(static_cast<size_t>(required), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required,
		nullptr, nullptr);
	return result;
}

void appendUtf8(std::string& out, unsigned codePoint)
{
	if (codePoint <= 0x7f) {
		out.push_back(static_cast<char>(codePoint));
	}
	else if (codePoint <= 0x7ff) {
		out.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
		out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	}
	else {
		out.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
		out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	}
}

[[nodiscard]] std::optional<std::string> jsonString(std::string_view json, std::string_view key)
{
	const std::string marker = "\"" + std::string(key) + "\"";
	auto at = json.find(marker);
	if (at == std::string_view::npos) {
		return std::nullopt;
	}
	at = json.find(':', at + marker.size());
	if (at == std::string_view::npos) {
		return std::nullopt;
	}
	while (++at < json.size() && std::isspace(static_cast<unsigned char>(json[at])) != 0) {}
	if (at >= json.size() || json[at] != '"') {
		return std::nullopt;
	}
	std::string result;
	for (++at; at < json.size(); ++at) {
		const char ch = json[at];
		if (ch == '"') {
			return result;
		}
		if (ch != '\\' || ++at >= json.size()) {
			result.push_back(ch);
			continue;
		}
		switch (json[at]) {
		case '"': result.push_back('"'); break;
		case '\\': result.push_back('\\'); break;
		case '/': result.push_back('/'); break;
		case 'b': result.push_back('\b'); break;
		case 'f': result.push_back('\f'); break;
		case 'n': result.push_back('\n'); break;
		case 'r': result.push_back('\r'); break;
		case 't': result.push_back('\t'); break;
		case 'u': {
			if (at + 4 >= json.size()) {
				return std::nullopt;
			}
			unsigned value = 0;
			for (int index = 0; index < 4; ++index) {
				const char digit = json[++at];
				value <<= 4;
				if (digit >= '0' && digit <= '9') value += static_cast<unsigned>(digit - '0');
				else if (digit >= 'a' && digit <= 'f') value += static_cast<unsigned>(digit - 'a' + 10);
				else if (digit >= 'A' && digit <= 'F') value += static_cast<unsigned>(digit - 'A' + 10);
				else return std::nullopt;
			}
			appendUtf8(result, value);
			break;
		}
		default: result.push_back(json[at]); break;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::wstring trim(std::wstring value)
{
	const auto notSpace = [](wchar_t ch) { return std::iswspace(ch) == 0; };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
	value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
	return value;
}

void addUnique(std::vector<std::wstring>& values, std::wstring value)
{
	if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
		values.emplace_back(std::move(value));
	}
}

[[nodiscard]] std::wstring join(const std::vector<std::wstring>& values, std::wstring_view separator)
{
	std::wstring result;
	for (const auto& value : values) {
		if (!result.empty()) result.append(separator);
		result.append(value);
	}
	return result;
}

[[nodiscard]] std::optional<std::string> httpGet(std::wstring_view host, std::wstring_view path,
	std::stop_token stopToken)
{
	if (stopToken.stop_requested()) return std::nullopt;
	InternetHandle session{ WinHttpOpen(WinMTRBranding::http_user_agent.data(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
	if (!session) return std::nullopt;
	WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 3500);
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
		WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

	InternetHandle connection{ WinHttpConnect(session.get(), std::wstring(host).c_str(),
		INTERNET_DEFAULT_HTTPS_PORT, 0) };
	if (!connection || stopToken.stop_requested()) return std::nullopt;
	InternetHandle request{ WinHttpOpenRequest(connection.get(), L"GET", std::wstring(path).c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH) };
	if (!request) return std::nullopt;
	const wchar_t headers[] = L"Accept: application/json, text/plain;q=0.9\r\nAccept-Encoding: identity\r\n";
	if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.get(), nullptr)) {
		return std::nullopt;
	}
	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status < 200 || status >= 300) {
		return std::nullopt;
	}
	std::string body;
	for (;;) {
		if (stopToken.stop_requested()) return std::nullopt;
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.get(), &available)) return std::nullopt;
		if (available == 0) break;
		const size_t oldSize = body.size();
		body.resize(oldSize + available);
		DWORD read = 0;
		if (!WinHttpReadData(request.get(), body.data() + oldSize, available, &read)) return std::nullopt;
		body.resize(oldSize + read);
		if (body.size() > 1024 * 1024) return std::nullopt;
	}
	return body;
}

[[nodiscard]] bool parseAddress(const std::wstring& value, SOCKADDR_INET& address) noexcept
{
	address = {};
	if (InetPtonW(AF_INET, value.c_str(), &address.Ipv4.sin_addr) == 1) {
		address.Ipv4.sin_family = AF_INET;
		return true;
	}
	if (InetPtonW(AF_INET6, value.c_str(), &address.Ipv6.sin6_addr) == 1) {
		address.Ipv6.sin6_family = AF_INET6;
		return true;
	}
	return false;
}

[[nodiscard]] bool expectedFamily(const std::wstring& value, ADDRESS_FAMILY family) noexcept
{
	SOCKADDR_INET address{};
	return parseAddress(value, address) && address.si_family == family;
}

[[nodiscard]] std::wstring reverseName(const std::wstring& value)
{
	SOCKADDR_INET address{};
	if (!parseAddress(value, address)) return {};
	wchar_t name[NI_MAXHOST]{};
	if (GetNameInfoW(reinterpret_cast<const sockaddr*>(&address),
		address.si_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), name,
		static_cast<DWORD>(std::size(name)), nullptr, 0, NI_NAMEREQD) == 0) {
		return name;
	}
	return {};
}

thread_local std::wstring g_geoCode;
thread_local std::wstring g_geoName;

BOOL CALLBACK enumGeoCallback(GEOID geoId)
{
	wchar_t code[16]{};
	if (GetGeoInfoW(geoId, GEO_ISO2, code, static_cast<int>(std::size(code)), 0) <= 0 ||
		_wcsicmp(code, g_geoCode.c_str()) != 0) {
		return TRUE;
	}
	wchar_t friendly[128]{};
	if (GetGeoInfoW(geoId, GEO_FRIENDLYNAME, friendly, static_cast<int>(std::size(friendly)), 0) > 0) {
		g_geoName = friendly;
	}
	return FALSE;
}

[[nodiscard]] std::wstring localizedCountry(std::wstring code)
{
	code = trim(std::move(code));
	if (code.empty()) return {};
	std::transform(code.begin(), code.end(), code.begin(), [](wchar_t ch) { return std::towupper(ch); });
	// Windows 7's geographic database knows ISO-3166 names and localizes the
	// friendly name.  Temporarily select zh-TW for this worker so an English
	// Windows installation still produces the product's requested locale.
	const LANGID previousUiLanguage = GetThreadUILanguage();
	const LCID previousLocale = GetThreadLocale();
	const LANGID traditionalChinese = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
	SetThreadUILanguage(traditionalChinese);
	SetThreadLocale(MAKELCID(traditionalChinese, SORT_DEFAULT));
	g_geoCode = code;
	g_geoName.clear();
	EnumSystemGeoID(GEOCLASS_NATION, 0, enumGeoCallback);
	SetThreadLocale(previousLocale);
	SetThreadUILanguage(previousUiLanguage);
	if (!g_geoName.empty()) return g_geoName;
	return code;
}

void parseOrganization(std::wstring organization, IpConnectionInfo& info)
{
	organization = trim(std::move(organization));
	if (organization.size() > 2 && (organization[0] == L'A' || organization[0] == L'a') &&
		(organization[1] == L'S' || organization[1] == L's')) {
		auto separator = organization.find_first_of(L" \t");
		info.asn = organization.substr(2, separator == std::wstring::npos ? std::wstring::npos : separator - 2);
		if (separator != std::wstring::npos) info.isp = trim(organization.substr(separator + 1));
	}
	else if (!organization.empty()) {
		info.isp = std::move(organization);
	}
}

[[nodiscard]] IpConnectionInfo parseIpInfo(const std::string& json)
{
	IpConnectionInfo info;
	const auto assign = [&](std::wstring& target, std::string_view key) {
		if (const auto value = jsonString(json, key)) target = utf8ToWide(*value);
	};
	assign(info.address, "ip");
	assign(info.hostname, "hostname");
	assign(info.city, "city");
	assign(info.region, "region");
	assign(info.countryCode, "country");
	if (const auto organization = jsonString(json, "org")) parseOrganization(utf8ToWide(*organization), info);
	return info;
}

[[nodiscard]] IpConnectionInfo parseIpApi(const std::string& json)
{
	IpConnectionInfo info;
	const auto assign = [&](std::wstring& target, std::string_view key) {
		if (const auto value = jsonString(json, key)) target = utf8ToWide(*value);
	};
	assign(info.address, "ip");
	assign(info.city, "city");
	assign(info.region, "region");
	assign(info.countryCode, "country_code");
	assign(info.country, "country_name");
	assign(info.asn, "asn");
	assign(info.isp, "org");
	if (info.asn.size() > 2 && _wcsnicmp(info.asn.c_str(), L"AS", 2) == 0) info.asn.erase(0, 2);
	return info;
}

[[nodiscard]] std::wstring percentEncode(std::wstring_view value)
{
	static constexpr char digits[] = "0123456789ABCDEF";
	const auto utf8 = wideToUtf8(value);
	std::wstring out;
	for (const unsigned char ch : utf8) {
		if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
			out.push_back(static_cast<wchar_t>(ch));
		}
		else {
			out.push_back(L'%');
			out.push_back(static_cast<wchar_t>(digits[ch >> 4]));
			out.push_back(static_cast<wchar_t>(digits[ch & 0x0f]));
		}
	}
	return out;
}

[[nodiscard]] std::vector<std::wstring> dnsTxt(const std::wstring& name)
{
	DNS_RECORDW* rawRecords = nullptr;
	if (DnsQuery_W(name.c_str(), DNS_TYPE_TEXT, DNS_QUERY_STANDARD, nullptr, &rawRecords, nullptr) != ERROR_SUCCESS) {
		return {};
	}
	DnsRecordList records{ rawRecords };
	std::vector<std::wstring> values;
	for (auto record = records.get(); record != nullptr; record = record->pNext) {
		if (record->wType != DNS_TYPE_TEXT || record->Data.TXT.dwStringCount == 0) continue;
		std::wstring value;
		for (DWORD index = 0; index < record->Data.TXT.dwStringCount; ++index) {
			if (index != 0) value.push_back(L' ');
			value.append(record->Data.TXT.pStringArray[index]);
		}
		values.emplace_back(trim(std::move(value)));
	}
	return values;
}

[[nodiscard]] std::wstring cymruOriginName(const std::wstring& address)
{
	SOCKADDR_INET parsed{};
	if (!parseAddress(address, parsed)) return {};
	std::wostringstream out;
	if (parsed.si_family == AF_INET) {
		const auto* bytes = reinterpret_cast<const unsigned char*>(&parsed.Ipv4.sin_addr);
		out << static_cast<unsigned>(bytes[3]) << L'.' << static_cast<unsigned>(bytes[2]) << L'.'
			<< static_cast<unsigned>(bytes[1]) << L'.' << static_cast<unsigned>(bytes[0])
			<< WinMTRBranding::sources::team_cymru_ipv4_suffix;
	}
	else {
		const auto* bytes = reinterpret_cast<const unsigned char*>(&parsed.Ipv6.sin6_addr);
		static constexpr wchar_t digits[] = L"0123456789abcdef";
		for (int index = 15; index >= 0; --index) {
			out << digits[bytes[index] & 0x0f] << L'.' << digits[bytes[index] >> 4] << L'.';
		}
		out << WinMTRBranding::sources::team_cymru_ipv6_suffix;
	}
	return out.str();
}

[[nodiscard]] std::pair<std::wstring, std::wstring> queryCymru(const std::wstring& address)
{
	const auto origin = cymruOriginName(address);
	if (origin.empty()) return {};
	const auto responses = dnsTxt(origin);
	if (responses.empty()) return {};
	auto first = responses.front();
	auto delimiter = first.find(L'|');
	std::wstring asn = trim(first.substr(0, delimiter));
	if (asn.size() > 2 && _wcsnicmp(asn.c_str(), L"AS", 2) == 0) asn.erase(0, 2);
	if (asn.empty()) return {};
	const auto names = dnsTxt(L"AS" + asn + std::wstring(WinMTRBranding::sources::team_cymru_asn_suffix));
	std::wstring provider;
	if (!names.empty()) {
		auto line = names.front();
		auto lastDelimiter = line.rfind(L'|');
		if (lastDelimiter != std::wstring::npos) provider = trim(line.substr(lastDelimiter + 1));
	}
	return { asn, provider };
}

void mergeMissing(IpConnectionInfo& target, const IpConnectionInfo& fallback)
{
	if (target.address.empty()) target.address = fallback.address;
	if (target.hostname.empty()) target.hostname = fallback.hostname;
	if (target.city.empty()) target.city = fallback.city;
	if (target.region.empty()) target.region = fallback.region;
	if (target.country.empty()) target.country = fallback.country;
	if (target.countryCode.empty()) target.countryCode = fallback.countryCode;
	if (target.asn.empty()) target.asn = fallback.asn;
	if (target.isp.empty()) target.isp = fallback.isp;
}

void enrich(IpConnectionInfo& info, std::vector<std::wstring>& sources, std::stop_token stopToken,
	bool allowIpApi, bool resolveHostname)
{
	if (!info.available() || stopToken.stop_requested()) return;
	std::vector<std::wstring> ownSources;
	if (!info.source.empty()) ownSources.emplace_back(info.source);
	const bool metadataMissing = info.city.empty() || info.region.empty() || info.countryCode.empty() ||
		info.asn.empty() || info.isp.empty();
	if (allowIpApi && metadataMissing) {
		const auto body = httpGet(WinMTRBranding::sources::ipapi_name,
			L"/" + percentEncode(info.address) + L"/json/", stopToken);
		if (body) {
			auto fallback = parseIpApi(*body);
			if (fallback.available()) {
				mergeMissing(info, fallback);
				addUnique(sources, std::wstring(WinMTRBranding::sources::ipapi_name));
				addUnique(ownSources, std::wstring(WinMTRBranding::sources::ipapi_name));
			}
		}
	}
	if ((info.asn.empty() || info.isp.empty()) && isPublicAddress(info.address) && !stopToken.stop_requested()) {
		const auto [asn, provider] = queryCymru(info.address);
		if (!asn.empty()) {
			if (info.asn.empty()) info.asn = asn;
			if (info.isp.empty()) info.isp = provider;
			addUnique(sources, std::wstring(WinMTRBranding::sources::team_cymru_name));
			addUnique(ownSources, std::wstring(WinMTRBranding::sources::team_cymru_name));
		}
	}
	if (resolveHostname && info.hostname.empty() && !stopToken.stop_requested()) {
		info.hostname = reverseName(info.address);
	}
	if (!info.countryCode.empty()) info.country = localizedCountry(info.countryCode);
	info.source = join(ownSources, L"、");
}

[[nodiscard]] std::vector<std::wstring> localDnsServers()
{
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buffer(size);
	auto addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
	ULONG result = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME, nullptr, addresses, &size);
	if (result == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(size);
		addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		result = GetAdaptersAddresses(AF_UNSPEC,
			GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME, nullptr, addresses, &size);
	}
	std::vector<std::wstring> servers;
	if (result != NO_ERROR) return servers;
	for (auto adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp) continue;
		for (auto dns = adapter->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
			wchar_t text[INET6_ADDRSTRLEN]{};
			const auto socketAddress = dns->Address.lpSockaddr;
			const void* rawAddress = nullptr;
			if (socketAddress->sa_family == AF_INET) rawAddress = &reinterpret_cast<sockaddr_in*>(socketAddress)->sin_addr;
			else if (socketAddress->sa_family == AF_INET6) rawAddress = &reinterpret_cast<sockaddr_in6*>(socketAddress)->sin6_addr;
			if (rawAddress != nullptr && InetNtopW(socketAddress->sa_family, rawAddress, text,
				static_cast<DWORD>(std::size(text))) != nullptr) {
				addUnique(servers, text);
			}
		}
	}
	return servers;
}

void queryWhoAmI(DnsConnectionInfo& info, std::vector<std::wstring>& sources)
{
	const auto records = dnsTxt(std::wstring(WinMTRBranding::sources::akahelp_host));
	if (records.empty()) {
		info.ecsSupport = EcsSupport::unknown;
		return;
	}
	info.source = WinMTRBranding::sources::akahelp_name;
	addUnique(sources, info.source);
	info.ecsSupport = EcsSupport::unsupported;
	for (const auto& record : records) {
		auto separator = record.find_first_of(L" \t");
		const auto key = separator == std::wstring::npos ? record : record.substr(0, separator);
		const auto value = separator == std::wstring::npos ? std::wstring{} : trim(record.substr(separator + 1));
		if (_wcsicmp(key.c_str(), L"ns") == 0 && info.publicAddress.empty()) info.publicAddress = value;
		else if (_wcsicmp(key.c_str(), L"ecs") == 0 && !value.empty()) {
			info.ecsSubnet = value;
			info.ecsSupport = EcsSupport::supported;
		}
	}
}

[[nodiscard]] const std::wstring& shown(const std::wstring& value)
{
	static const std::wstring unavailable = std::wstring(WinMTRBranding::network_strings::unavailable);
	return value.empty() ? unavailable : value;
}

} // namespace

bool isPublicAddress(const std::wstring& address) noexcept
{
	SOCKADDR_INET parsed{};
	if (!parseAddress(address, parsed)) return false;
	if (parsed.si_family == AF_INET) {
		const auto hostOrder = ntohl(parsed.Ipv4.sin_addr.S_un.S_addr);
		const unsigned first = (hostOrder >> 24) & 0xff;
		const unsigned second = (hostOrder >> 16) & 0xff;
		if (first == 0 || first == 10 || first == 127 || first >= 224) return false;
		if (first == 169 && second == 254) return false;
		if (first == 172 && second >= 16 && second <= 31) return false;
		if (first == 192 && second == 168) return false;
		if (first == 100 && second >= 64 && second <= 127) return false;
		const auto inPrefix = [hostOrder](std::uint32_t network, std::uint32_t mask) noexcept {
			return (hostOrder & mask) == network;
		};
		if (inPrefix(0xc0000000u, 0xffffff00u)       // 192.0.0.0/24 protocol assignments
			|| inPrefix(0xc0000200u, 0xffffff00u)    // TEST-NET-1
			|| inPrefix(0xc0586300u, 0xffffff00u)    // deprecated 6to4 relay anycast
			|| inPrefix(0xc6120000u, 0xfffe0000u)    // benchmarking
			|| inPrefix(0xc6336400u, 0xffffff00u)    // TEST-NET-2
			|| inPrefix(0xcb007100u, 0xffffff00u)) { // TEST-NET-3
			return false;
		}
		return true;
	}
	const auto* bytes = reinterpret_cast<const unsigned char*>(&parsed.Ipv6.sin6_addr);
	// Only globally routable unicast space is suitable for external metadata
	// services.  Exclude the documentation, benchmarking and ORCHID blocks
	// that live inside 2000::/3 as well.
	if ((bytes[0] & 0xe0) != 0x20) return false;
	const auto prefix = [bytes](std::initializer_list<unsigned char> value, unsigned bits) noexcept {
		unsigned bit = 0;
		for (const unsigned char expected : value) {
			if (bit >= bits) break;
			const unsigned used = std::min(8u, bits - bit);
			const unsigned char mask = static_cast<unsigned char>(0xffu << (8u - used));
			if ((bytes[bit / 8] & mask) != (expected & mask)) return false;
			bit += used;
		}
		return bit >= bits;
	};
	if (prefix({ 0x20, 0x01, 0x00, 0x02, 0x00, 0x00 }, 48) // benchmarking
		|| prefix({ 0x20, 0x01, 0x00, 0x10 }, 28)             // ORCHIDv1
		|| prefix({ 0x20, 0x01, 0x00, 0x20 }, 28)             // ORCHIDv2
		|| prefix({ 0x20, 0x01, 0x0d, 0xb8 }, 32)             // documentation
		|| prefix({ 0x3f, 0xff, 0x00 }, 20)) {                // documentation
		return false;
	}
	return true;
}

IpConnectionInfo queryAddress(const std::wstring& address, std::stop_token stopToken,
	bool resolveHostname)
{
	IpConnectionInfo result;
	if (!isPublicAddress(address) || stopToken.stop_requested()) return result;
	result.address = address;
	std::vector<std::wstring> sources;
	enrich(result, sources, stopToken, true, resolveHostname);
	return result;
}

CurrentNetworkInfo queryCurrent(std::stop_token stopToken)
{
	CurrentNetworkInfo result;
	WSADATA wsaData{};
	const bool wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
	struct WsaCleanup final {
		bool active;
		~WsaCleanup() { if (active) WSACleanup(); }
	} cleanup{ wsaStarted };

	if (!stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipinfo_ipv4_name, L"/json", stopToken)) {
			auto info = parseIpInfo(*body);
			if (expectedFamily(info.address, AF_INET)) {
				info.source = WinMTRBranding::sources::ipinfo_ipv4_name;
				result.ipv4 = std::move(info);
				addUnique(result.successfulSources, std::wstring(WinMTRBranding::sources::ipinfo_ipv4_name));
			}
		}
	}
	if (!result.ipv4.available() && !stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipify_ipv4_name, L"/", stopToken)) {
			const auto address = trim(utf8ToWide(*body));
			if (expectedFamily(address, AF_INET)) {
				result.ipv4.address = address;
				result.ipv4.source = WinMTRBranding::sources::ipify_ipv4_name;
				addUnique(result.successfulSources, std::wstring(WinMTRBranding::sources::ipify_ipv4_name));
			}
		}
	}
	if (result.ipv4.available()) enrich(result.ipv4, result.successfulSources, stopToken, true, true);

	if (!stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipinfo_ipv6_name, L"/json", stopToken)) {
			auto info = parseIpInfo(*body);
			if (expectedFamily(info.address, AF_INET6)) {
				info.source = WinMTRBranding::sources::ipinfo_ipv6_name;
				result.ipv6 = std::move(info);
				addUnique(result.successfulSources, std::wstring(WinMTRBranding::sources::ipinfo_ipv6_name));
			}
		}
	}
	if (!result.ipv6.available() && !stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipify_ipv6_name, L"/", stopToken)) {
			const auto address = trim(utf8ToWide(*body));
			if (expectedFamily(address, AF_INET6)) {
				result.ipv6.address = address;
				result.ipv6.source = WinMTRBranding::sources::ipify_ipv6_name;
				addUnique(result.successfulSources, std::wstring(WinMTRBranding::sources::ipify_ipv6_name));
			}
		}
	}
	if (result.ipv6.available()) enrich(result.ipv6, result.successfulSources, stopToken, true, true);

	if (!stopToken.stop_requested()) {
		queryWhoAmI(result.dns, result.successfulSources);
		result.dns.localServers = localDnsServers();
		if (!result.dns.localServers.empty()) {
			addUnique(result.successfulSources, std::wstring(WinMTRBranding::sources::windows_network_name));
		}
		if (isPublicAddress(result.dns.publicAddress)) {
			auto dnsMetadata = queryAddress(result.dns.publicAddress, stopToken, false);
			result.dns.provider = dnsMetadata.isp;
			result.dns.location = join(std::vector<std::wstring>{ dnsMetadata.city, dnsMetadata.country }, L"，");
			if (!dnsMetadata.source.empty()) {
				for (const auto& source : std::vector<std::wstring>{
					std::wstring(WinMTRBranding::sources::ipapi_name),
					std::wstring(WinMTRBranding::sources::team_cymru_name) }) {
					if (dnsMetadata.source.find(source) != std::wstring::npos) addUnique(result.successfulSources, source);
				}
			}
		}
	}
	result.complete = true;
	return result;
}

std::wstring formatDetails(const CurrentNetworkInfo& info)
{
	using namespace WinMTRBranding::network_strings;
	std::wostringstream out;
	const auto section = [&](std::wstring_view title, const IpConnectionInfo& value) {
		out << title << L"：\r\n"
			<< L"  " << address << L"：" << shown(value.address) << L"\r\n"
			<< L"  " << hostname << L"：" << shown(value.hostname) << L"\r\n"
			<< L"  " << city << L"：" << shown(value.city) << L"\r\n"
			<< L"  " << region << L"：" << shown(value.region) << L"\r\n"
			<< L"  " << country << L"：" << shown(value.country) << L"\r\n"
			<< L"  " << asn << L"：" << shown(value.asn) << L"\r\n"
			<< L"  " << provider << L"：" << shown(value.isp) << L"\r\n";
	};
	section(ipv4_section, info.ipv4);
	out << L"\r\n";
	section(ipv6_section, info.ipv6);
	out << L"\r\n" << recursive_dns_section << L"：\r\n"
		<< L"  " << dns_public_address << L"：" << shown(info.dns.publicAddress) << L"\r\n"
		<< L"  " << dns_location_provider << L"：";
	std::vector<std::wstring> dnsDescription;
	if (!info.dns.location.empty()) dnsDescription.push_back(info.dns.location);
	if (!info.dns.provider.empty()) dnsDescription.push_back(info.dns.provider);
	out << shown(join(dnsDescription, L"／")) << L"\r\n"
		<< L"  " << ecs << L"：";
	switch (info.dns.ecsSupport) {
	case EcsSupport::supported:
		out << ecs_supported;
		if (!info.dns.ecsSubnet.empty()) out << L"：" << info.dns.ecsSubnet;
		break;
	case EcsSupport::unsupported: out << ecs_unsupported; break;
	default: out << ecs_unknown; break;
	}
	out << L"\r\n  " << local_dns << L"：" << shown(join(info.dns.localServers, L"、")) << L"\r\n"
		<< L"\r\n" << sources << L"：" << shown(join(info.successfulSources, L"、")) << L"\r\n";
	return out.str();
}

} // namespace winmtr::network_data
