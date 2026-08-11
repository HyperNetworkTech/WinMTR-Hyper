#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <VersionHelpers.h>
#include <WinDNS.h>
#include <WinHTTP.h>
#include <Iphlpapi.h>

#include "WinMTRNetworkData.h"
#include "WinMTRBranding.h"
#include "WinMTRJson.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
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

struct CancelableInternetState final {
	explicit CancelableInternetState(void* initial) noexcept : value(initial) {}
	~CancelableInternetState() noexcept { close(); }
	void close() noexcept
	{
		if (void* handle = value.exchange(nullptr, std::memory_order_acq_rel)) {
			WinHttpCloseHandle(handle);
		}
	}
	std::atomic<void*> value = nullptr;
};

class CancelableInternetHandle final {
public:
	explicit CancelableInternetHandle(void* value)
		: state_(std::make_shared<CancelableInternetState>(value)) {}
	[[nodiscard]] void* get() const noexcept
	{
		return state_->value.load(std::memory_order_acquire);
	}
	[[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }
	[[nodiscard]] std::shared_ptr<CancelableInternetState> state() const noexcept
	{
		return state_;
	}
private:
	std::shared_ptr<CancelableInternetState> state_;
};

struct ProviderHealth final {
	unsigned consecutiveFailures = 0;
	std::uint64_t retryAfterTick = 0;
};

std::mutex providerHealthMutex;
std::unordered_map<std::wstring, ProviderHealth> providerHealth;

[[nodiscard]] bool providerAvailable(std::wstring_view provider)
{
	const auto now = GetTickCount64();
	std::scoped_lock lock(providerHealthMutex);
	const auto found = providerHealth.find(std::wstring(provider));
	return found == providerHealth.end() || found->second.retryAfterTick == 0
		|| now >= found->second.retryAfterTick;
}

void recordProviderResult(std::wstring_view provider, bool success)
{
	std::scoped_lock lock(providerHealthMutex);
	auto& health = providerHealth[std::wstring(provider)];
	if (success) {
		health = {};
		return;
	}
	health.consecutiveFailures = std::min(health.consecutiveFailures + 1u, 10u);
	const auto exponent = std::min(health.consecutiveFailures - 1u, 8u);
	const auto baseDelayMs = std::min<std::uint64_t>(300'000ull, 1'000ull << exponent);
	const auto jitterMs = std::hash<std::wstring_view>{}(provider) % 251u;
	health.retryAfterTick = GetTickCount64() + baseDelayMs + jitterMs;
}

class QueryBudget final {
public:
	QueryBudget(std::stop_token parent, std::chrono::milliseconds duration)
		: forwardParent_(parent, [this] { cancellation_.request_stop(); }),
		  timer_([this, duration](std::stop_token timerStop) {
			  std::mutex mutex;
			  std::condition_variable_any changed;
			  std::unique_lock lock(mutex);
			  (void)changed.wait_for(lock, timerStop, duration, [] { return false; });
			  if (!timerStop.stop_requested()) cancellation_.request_stop();
		  })
	{
	}

	[[nodiscard]] std::stop_token token() const noexcept
	{
		return cancellation_.get_token();
	}

private:
	std::stop_source cancellation_;
	std::stop_callback<std::function<void()>> forwardParent_;
	std::jthread timer_;
};

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

[[nodiscard]] std::optional<std::string> jsonString(std::string_view json, std::string_view key)
{
	return winmtr::json::get_string(json, key);
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
	if (stopToken.stop_requested() || !providerAvailable(host)) return std::nullopt;
	const auto failed = [host]() -> std::optional<std::string> {
		recordProviderResult(host, false);
		return std::nullopt;
	};
	InternetHandle session{ WinHttpOpen(WinMTRBranding::http_user_agent.data(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
	if (!session) return failed();
	WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 3500);
	if (!IsWindows10OrGreater()) {
		DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
		if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS,
			&protocols, sizeof(protocols))) return failed();
	}

	InternetHandle connection{ WinHttpConnect(session.get(), std::wstring(host).c_str(),
		INTERNET_DEFAULT_HTTPS_PORT, 0) };
	if (!connection || stopToken.stop_requested()) return failed();
	CancelableInternetHandle request{ WinHttpOpenRequest(connection.get(), L"GET", std::wstring(path).c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH) };
	if (!request) return failed();
	std::stop_callback cancelRequest(stopToken,
		[state = request.state()] { state->close(); });
	if (stopToken.stop_requested()) return std::nullopt;
	const wchar_t headers[] = L"Accept: application/json, text/plain;q=0.9\r\nAccept-Encoding: identity\r\n";
	if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.get(), nullptr)) {
		return stopToken.stop_requested() ? std::nullopt : failed();
	}
	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status < 200 || status >= 300) {
		return failed();
	}
	std::string body;
	for (;;) {
		if (stopToken.stop_requested()) return std::nullopt;
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.get(), &available)) {
			return stopToken.stop_requested() ? std::nullopt : failed();
		}
		if (available == 0) break;
		const size_t oldSize = body.size();
		body.resize(oldSize + available);
		DWORD read = 0;
		if (!WinHttpReadData(request.get(), body.data() + oldSize, available, &read)) {
			return stopToken.stop_requested() ? std::nullopt : failed();
		}
		body.resize(oldSize + read);
		if (body.size() > 1024 * 1024) return failed();
	}
	recordProviderResult(host, true);
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
	const auto providerName = WinMTRBranding::sources::team_cymru_name;
	if (!providerAvailable(providerName)) return {};
	const auto origin = cymruOriginName(address);
	if (origin.empty()) return {};
	const auto responses = dnsTxt(origin);
	if (responses.empty()) {
		recordProviderResult(providerName, false);
		return {};
	}
	auto first = responses.front();
	auto delimiter = first.find(L'|');
	std::wstring asn = trim(first.substr(0, delimiter));
	if (asn.size() > 2 && _wcsnicmp(asn.c_str(), L"AS", 2) == 0) asn.erase(0, 2);
	if (asn.empty()) {
		recordProviderResult(providerName, false);
		return {};
	}
	const auto names = dnsTxt(L"AS" + asn + std::wstring(WinMTRBranding::sources::team_cymru_asn_suffix));
	std::wstring provider;
	if (!names.empty()) {
		auto line = names.front();
		auto lastDelimiter = line.rfind(L'|');
		if (lastDelimiter != std::wstring::npos) provider = trim(line.substr(lastDelimiter + 1));
	}
	recordProviderResult(providerName, true);
	return { asn, provider };
}

[[nodiscard]] bool hasMetadata(const IpConnectionInfo& info) noexcept
{
	return !info.hostname.empty() || !info.city.empty() || !info.region.empty()
		|| !info.country.empty() || !info.countryCode.empty() || !info.asn.empty()
		|| !info.isp.empty();
}

void finalize(IpConnectionInfo& info, std::stop_token stopToken, bool resolveHostname)
{
	if (!info.available()) return;
	if (resolveHostname && info.hostname.empty() && !stopToken.stop_requested()) {
		info.hostname = reverseName(info.address);
	}
	if (!info.countryCode.empty()) info.country = localizedCountry(info.countryCode);
}

[[nodiscard]] IpConnectionInfo queryCurrentFamily(ADDRESS_FAMILY family, std::stop_token stopToken)
{
	IpConnectionInfo selected;
	const auto primaryName = family == AF_INET
		? WinMTRBranding::sources::ipinfo_ipv4_name
		: WinMTRBranding::sources::ipinfo_ipv6_name;
	if (!stopToken.stop_requested()) {
		if (const auto body = httpGet(primaryName, L"/json", stopToken)) {
			auto candidate = parseIpInfo(*body);
			if (expectedFamily(candidate.address, family)) {
				candidate.source = primaryName;
				selected = std::move(candidate);
			}
		}
	}

	// A fallback is selected only when the primary source did not return a
	// usable address. Never merge fields from two services into one result.
	if (!selected.available() && !stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipapi_name, L"/json/", stopToken)) {
			auto candidate = parseIpApi(*body);
			if (expectedFamily(candidate.address, family)) {
				candidate.source = WinMTRBranding::sources::ipapi_name;
				selected = std::move(candidate);
			}
		}
	}
	if (!selected.available() && !stopToken.stop_requested()) {
		const auto fallbackName = family == AF_INET
			? WinMTRBranding::sources::ipify_ipv4_name
			: WinMTRBranding::sources::ipify_ipv6_name;
		if (const auto body = httpGet(fallbackName, L"/", stopToken)) {
			const auto address = trim(utf8ToWide(*body));
			if (expectedFamily(address, family)) {
				selected.address = address;
				selected.source = fallbackName;
			}
		}
	}
	finalize(selected, stopToken, true);
	if (selected.source.empty()) {
		selected.failureReason = L"No provider returned a usable address for the requested family";
	}
	else if (selected.source != primaryName) {
		selected.failureReason = std::wstring(primaryName)
			+ L" was unavailable or returned an unusable response";
	}
	return selected;
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
	bool resolveHostname, bool allowHttpFallback)
{
	IpConnectionInfo result;
	if (!isPublicAddress(address) || stopToken.stop_requested()) return result;
	SOCKADDR_INET parsed{};
	(void)parseAddress(address, parsed);
	const auto [asn, provider] = queryCymru(address);
	if (!asn.empty()) {
		result.address = address;
		result.asn = asn;
		result.isp = provider;
		result.source = WinMTRBranding::sources::team_cymru_name;
	}
	const auto primaryName = parsed.si_family == AF_INET6
		? WinMTRBranding::sources::ipinfo_ipv6_name
		: WinMTRBranding::sources::ipinfo_ipv4_name;
	if (!result.available() && allowHttpFallback) {
		if (const auto body = httpGet(primaryName,
			L"/" + percentEncode(address) + L"/json", stopToken)) {
			auto candidate = parseIpInfo(*body);
			if (hasMetadata(candidate)) {
				candidate.address = address;
				candidate.source = primaryName;
				result = std::move(candidate);
			}
		}
	}
	if (!result.available() && allowHttpFallback && !stopToken.stop_requested()) {
		if (const auto body = httpGet(WinMTRBranding::sources::ipapi_name,
			L"/" + percentEncode(address) + L"/json/", stopToken)) {
			auto candidate = parseIpApi(*body);
			if (hasMetadata(candidate)) {
				candidate.address = address;
				candidate.source = WinMTRBranding::sources::ipapi_name;
				result = std::move(candidate);
			}
		}
	}
	if (!result.available() && !stopToken.stop_requested()) {
		result.failureReason = allowHttpFallback
			? L"Team Cymru DNS and configured HTTPS providers returned no usable metadata"
			: L"Team Cymru DNS returned no usable metadata; HTTPS fallback is disabled for hops";
	}
	finalize(result, stopToken, resolveHostname);
	return result;
}

CurrentNetworkInfo queryCurrent(std::stop_token stopToken)
{
	CurrentNetworkInfo result;
	QueryBudget budget(stopToken, std::chrono::seconds(8));
	const auto queryToken = budget.token();
	WSADATA wsaData{};
	const bool wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
	struct WsaCleanup final {
		bool active;
		~WsaCleanup() { if (active) WSACleanup(); }
	} cleanup{ wsaStarted };

	std::jthread ipv4Query([&] {
		try { result.ipv4 = queryCurrentFamily(AF_INET, queryToken); }
		catch (...) { result.ipv4.failureReason = L"IPv4 provider query failed"; }
	});
	std::jthread ipv6Query([&] {
		try { result.ipv6 = queryCurrentFamily(AF_INET6, queryToken); }
		catch (...) { result.ipv6.failureReason = L"IPv6 provider query failed"; }
	});

	if (!queryToken.stop_requested()) {
		queryWhoAmI(result.dns, result.successfulSources);
		result.dns.localServers = localDnsServers();
		if (isPublicAddress(result.dns.publicAddress)) {
			auto dnsMetadata = queryAddress(result.dns.publicAddress, queryToken, true, true);
			result.dns.hostname = dnsMetadata.hostname;
			result.dns.city = dnsMetadata.city;
			result.dns.region = dnsMetadata.region;
			result.dns.country = dnsMetadata.country;
			result.dns.countryCode = dnsMetadata.countryCode;
			result.dns.asn = dnsMetadata.asn;
			result.dns.provider = dnsMetadata.isp;
			result.dns.metadataSource = dnsMetadata.source;
			result.dns.failureReason = dnsMetadata.failureReason;
			if (!dnsMetadata.source.empty()) addUnique(result.successfulSources, dnsMetadata.source);
		}
	}
	if (ipv4Query.joinable()) ipv4Query.join();
	if (ipv6Query.joinable()) ipv6Query.join();
	if (!result.ipv4.source.empty()) addUnique(result.successfulSources, result.ipv4.source);
	if (!result.ipv6.source.empty()) addUnique(result.successfulSources, result.ipv6.source);
	result.updatedAtUnixMs = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	result.timedOut = queryToken.stop_requested() && !stopToken.stop_requested();
	if (!result.anyAvailable()) {
		result.refreshError = result.timedOut
			? L"The public network information query exceeded its 8-second budget"
			: L"No configured provider returned usable network information";
	}
	result.complete = true;
	return result;
}

std::wstring formatDetails(const CurrentNetworkInfo& info)
{
	using namespace WinMTRBranding::network_strings;
	std::wostringstream out;
	const auto field = [&](std::wstring_view label, const std::wstring& value) {
		out << label << L'\t' << shown(value) << L"\r\n";
	};
	const auto section = [&](std::wstring_view title, const IpConnectionInfo& value) {
		out << title << L"\r\n";
		field(address, value.address);
		field(hostname, value.hostname);
		field(city, value.city);
		field(region, value.region);
		field(country, value.country);
		field(asn, value.asn);
		field(provider, value.isp);
		field(sources, value.source);
		if (!value.failureReason.empty()) field(L"失敗原因", value.failureReason);
	};
	if (info.updatedAtUnixMs != 0) {
		field(L"Last updated (Unix ms)", std::to_wstring(info.updatedAtUnixMs));
	}
	if (info.refreshing) field(L"狀態", L"更新中（顯示上次成功資料）");
	else if (info.stale) field(L"狀態", L"更新失敗（顯示上次成功資料）");
	if (!info.refreshError.empty()) field(L"更新錯誤", info.refreshError);
	if (info.updatedAtUnixMs != 0 || info.refreshing || info.stale
		|| !info.refreshError.empty()) out << L"\r\n";
	section(ipv4_section, info.ipv4);
	out << L"\r\n";
	section(ipv6_section, info.ipv6);
	out << L"\r\n" << recursive_dns_section << L"\r\n";
	field(address, info.dns.publicAddress);
	field(hostname, info.dns.hostname);
	field(city, info.dns.city);
	field(region, info.dns.region);
	field(country, info.dns.country);
	field(asn, info.dns.asn);
	field(provider, info.dns.provider);
	field(sources, info.dns.source);
	if (!info.dns.metadataSource.empty()) field(L"Metadata source", info.dns.metadataSource);
	if (!info.dns.failureReason.empty()) field(L"失敗原因", info.dns.failureReason);
	std::wstring ecsValue;
	switch (info.dns.ecsSupport) {
	case EcsSupport::supported:
		ecsValue = ecs_supported;
		if (!info.dns.ecsSubnet.empty()) ecsValue.append(L"（").append(info.dns.ecsSubnet).append(L"）");
		break;
	case EcsSupport::unsupported: ecsValue = ecs_unsupported; break;
	default: ecsValue = ecs_unknown; break;
	}
	field(ecs, ecsValue);
	out << L"\r\n" << local_dns << L"\r\n";
	if (info.dns.localServers.empty()) out << L'\t' << unavailable << L"\r\n";
	else {
		for (const auto& server : info.dns.localServers) out << L'\t' << server << L"\r\n";
	}
	out << L"\r\n";
	field(sources, join(info.successfulSources, L"、"));
	return out.str();
}

} // namespace winmtr::network_data
