module;

#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <afx.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcmn.h>
#include <atlbase.h>
#include "resource.h"
#include "WinMTRBranding.h"

module WinMTR.Dialog:registry;

import :ClassDef;
import <algorithm>;
import <cstddef>;
import <format>;
import <iterator>;
import <string>;
import <vector>;
import WinMTR.Options;
import WinMTRUtils;

namespace {
constexpr wchar_t rootKeyName[] = LR"(Software\WinMTR)";
constexpr wchar_t configKeyName[] = LR"(Software\WinMTR\Config)";
constexpr wchar_t historyKeyName[] = LR"(Software\WinMTR\LRU)";
constexpr wchar_t historyCountName[] = L"NrLRU";

[[nodiscard]] CString localized(UINT id)
{
	CString value;
	value.LoadStringW(id);
	return value;
}

[[nodiscard]] DWORD queryDword(CRegKey& key, const wchar_t* name, DWORD fallback) noexcept
{
	DWORD value = fallback;
	if (key.QueryDWORDValue(name, value) != ERROR_SUCCESS) {
		value = fallback;
		key.SetDWORDValue(name, value);
	}
	return value;
}

void persistHistory(const std::vector<std::wstring>& hosts)
{
	CRegKey key;
	if (key.Create(HKEY_CURRENT_USER, historyKeyName) != ERROR_SUCCESS) return;
	DWORD oldCount = 0;
	key.QueryDWORDValue(historyCountName, oldCount);
	oldCount = std::min<DWORD>(oldCount, 4096);
	for (DWORD index = 1; index <= std::max<DWORD>(oldCount, static_cast<DWORD>(hosts.size())); ++index) {
		key.DeleteValue(std::format(L"Host{}", index).c_str());
	}
	for (size_t index = 0; index < hosts.size(); ++index) {
		key.SetStringValue(std::format(L"Host{}", index + 1).c_str(), hosts[index].c_str());
	}
	key.SetDWORDValue(historyCountName, static_cast<DWORD>(hosts.size()));
}

[[nodiscard]] std::vector<std::wstring> loadPersistedHistory()
{
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, historyKeyName, KEY_READ) != ERROR_SUCCESS) return {};
	DWORD storedCount = 0;
	if (key.QueryDWORDValue(historyCountName, storedCount) != ERROR_SUCCESS) return {};
	storedCount = std::min<DWORD>(storedCount, 4096);
	std::vector<std::wstring> hosts;
	for (DWORD index = 1; index <= storedCount; ++index) {
		wchar_t value[NI_MAXHOST]{};
		ULONG characters = static_cast<ULONG>(std::size(value));
		if (key.QueryStringValue(std::format(L"Host{}", index).c_str(), value, &characters) != ERROR_SUCCESS
			|| value[0] == L'\0') continue;
		if (const auto duplicate = std::find_if(hosts.begin(), hosts.end(), [&](const std::wstring& existing) {
			return _wcsicmp(existing.c_str(), value) == 0;
		}); duplicate != hosts.end()) {
			hosts.erase(duplicate);
		}
		hosts.emplace_back(value);
	}
	return hosts;
}

void keepNewest(std::vector<std::wstring>& hosts, size_t limit)
{
	if (hosts.size() > limit) {
		hosts.erase(hosts.begin(), hosts.end() - static_cast<std::ptrdiff_t>(limit));
	}
}

} // namespace

BOOL WinMTRDialog::InitRegistry() noexcept
{
	CRegKey root;
	if (root.Create(HKEY_CURRENT_USER, rootKeyName) != ERROR_SUCCESS) return FALSE;
	root.SetStringValue(L"Version", WinMTRBranding::display_version.data());
	root.SetStringValue(L"License", L"GNU General Public License v2");
	root.SetStringValue(L"HomePage", WinMTRBranding::company_url.data());

	CRegKey config;
	if (config.Create(HKEY_CURRENT_USER, configKeyName) != ERROR_SUCCESS) return FALSE;
	if (!hasPacketSizeFromCommandLine) packetSize = std::clamp<unsigned>(queryDword(config, L"PingSize",
		WinMTRUtils::DEFAULT_PING_SIZE), WinMTRUtils::MIN_PING_SIZE, WinMTRUtils::MAX_PING_SIZE);
	persistentHistoryLimit = std::clamp<unsigned>(queryDword(config, L"MaxLRU",
		WinMTRUtils::DEFAULT_MAX_LRU), WinMTRUtils::MIN_MAX_LRU, WinMTRUtils::MAX_MAX_LRU);
	if (!hasHistoryLimitFromCommandLine) historyLimit = persistentHistoryLimit;
	if (!hasResolveNamesFromCommandLine) resolveNames = queryDword(config, L"UseDNS",
		WinMTRUtils::DEFAULT_USE_DNS) != 0;
	if (!hasIntervalFromCommandLine) interval = std::clamp<double>(queryDword(config, L"IntervalMs",
		static_cast<DWORD>(WinMTRUtils::DEFAULT_INTERVAL * 1000.0)) / 1000.0,
		WinMTRUtils::MIN_INTERVAL, WinMTRUtils::MAX_INTERVAL);
	maxHops = std::clamp<unsigned>(queryDword(config, L"MaxHops", WinMTRUtils::DEFAULT_MAX_HOPS),
		WinMTRUtils::MIN_MAX_HOPS, WinMTRUtils::MAX_MAX_HOPS);
	timeoutMs = std::clamp<unsigned>(queryDword(config, L"TimeoutMs", WinMTRUtils::DEFAULT_TIMEOUT_MS),
		WinMTRUtils::MIN_TIMEOUT_MS, WinMTRUtils::MAX_TIMEOUT_MS);
	cycles = std::clamp<unsigned>(queryDword(config, L"Cycles", WinMTRUtils::DEFAULT_CYCLES),
		WinMTRUtils::MIN_CYCLES, WinMTRUtils::MAX_CYCLES);
	tos = std::clamp<unsigned>(queryDword(config, L"TOS", WinMTRUtils::DEFAULT_TOS),
		WinMTRUtils::MIN_TOS, WinMTRUtils::MAX_TOS);
	payloadPattern = std::clamp(static_cast<int>(queryDword(config, L"PayloadPattern",
		static_cast<DWORD>(WinMTRUtils::DEFAULT_PAYLOAD_PATTERN))), WinMTRUtils::MIN_PAYLOAD_PATTERN,
		WinMTRUtils::MAX_PAYLOAD_PATTERN);
	startTtl = std::clamp<unsigned>(queryDword(config, L"StartTTL", WinMTRUtils::DEFAULT_START_TTL),
		WinMTRUtils::MIN_START_TTL, maxHops.load());
	minimumTtl = std::clamp<unsigned>(queryDword(config, L"MinimumTTL", WinMTRUtils::DEFAULT_MINIMUM_TTL),
		WinMTRUtils::MIN_MINIMUM_TTL, maxHops.load());
	unknownHostLimit = std::clamp<unsigned>(queryDword(config, L"UnknownHostLimit",
		WinMTRUtils::DEFAULT_UNKNOWN_HOST_LIMIT), WinMTRUtils::MIN_UNKNOWN_HOST_LIMIT,
		WinMTRUtils::MAX_UNKNOWN_HOST_LIMIT);
	ecmpDisplayLimit = std::clamp<unsigned>(queryDword(config, L"EcmpDisplayLimit",
		WinMTRUtils::DEFAULT_ECMP_DISPLAY_LIMIT), WinMTRUtils::MIN_ECMP_DISPLAY_LIMIT,
		WinMTRUtils::MAX_ECMP_RESPONDERS);
	replyCacheSeconds = std::clamp<unsigned>(queryDword(config, L"ReplyCacheSeconds",
		WinMTRUtils::DEFAULT_REPLY_CACHE_SECONDS), WinMTRUtils::MIN_REPLY_CACHE_SECONDS,
		WinMTRUtils::MAX_REPLY_CACHE_SECONDS);
	lookupAsnIsp = queryDword(config, L"LookupAsnIsp", WinMTRUtils::DEFAULT_LOOKUP_ASN_ISP) != 0;
	dontFragment = queryDword(config, L"DontFragment", WinMTRUtils::DEFAULT_DONT_FRAGMENT) != 0;
	useIPv4 = queryDword(config, L"UseIPv4", WinMTRUtils::DEFAULT_USE_IPV4) != 0;
	useIPv6 = queryDword(config, L"UseIPv6", WinMTRUtils::DEFAULT_USE_IPV6) != 0;
	queryPublicInfo = queryDword(config, L"QueryPublicInfo",
		WinMTRUtils::DEFAULT_QUERY_PUBLIC_NETWORK_INFO) != 0;

	auto persistedHosts = loadPersistedHistory();
	if (!hasHistoryLimitFromCommandLine) {
		keepNewest(persistedHosts, persistentHistoryLimit);
		persistHistory(persistedHosts);
	}
	sessionHistory = persistedHosts;
	keepNewest(sessionHistory, std::max(historyLimit, persistentHistoryLimit));
	auto visibleHosts = sessionHistory;
	keepNewest(visibleHosts, historyLimit);
	for (const auto& host : visibleHosts) comboHost.AddString(host.c_str());
	historyCount = static_cast<int>(visibleHosts.size());
	comboHost.AddString(localized(IDS_STRING_CLEAR_HISTORY));
	return TRUE;
}

void WinMTRDialog::SaveSettings() noexcept
{
	CRegKey config;
	if (config.Create(HKEY_CURRENT_USER, configKeyName) != ERROR_SUCCESS) return;
	config.SetDWORDValue(L"PingSize", packetSize.load());
	config.SetDWORDValue(L"MaxLRU", historyLimit);
	config.SetDWORDValue(L"UseDNS", resolveNames.load() ? 1 : 0);
	config.SetDWORDValue(L"IntervalMs", static_cast<DWORD>(interval.load() * 1000.0 + 0.5));
	config.SetDWORDValue(L"MaxHops", maxHops.load());
	config.SetDWORDValue(L"TimeoutMs", timeoutMs.load());
	config.SetDWORDValue(L"Cycles", cycles.load());
	config.SetDWORDValue(L"TOS", tos.load());
	config.SetDWORDValue(L"PayloadPattern", static_cast<DWORD>(payloadPattern.load()));
	config.SetDWORDValue(L"StartTTL", startTtl.load());
	config.SetDWORDValue(L"MinimumTTL", minimumTtl.load());
	config.SetDWORDValue(L"UnknownHostLimit", unknownHostLimit.load());
	config.SetDWORDValue(L"EcmpDisplayLimit", ecmpDisplayLimit.load());
	config.SetDWORDValue(L"ReplyCacheSeconds", replyCacheSeconds.load());
	config.SetDWORDValue(L"LookupAsnIsp", lookupAsnIsp.load() ? 1 : 0);
	config.SetDWORDValue(L"DontFragment", dontFragment.load() ? 1 : 0);
	config.SetDWORDValue(L"UseIPv4", useIPv4.load() ? 1 : 0);
	config.SetDWORDValue(L"UseIPv6", useIPv6.load() ? 1 : 0);
	config.SetDWORDValue(L"QueryPublicInfo", queryPublicInfo.load() ? 1 : 0);
}

void WinMTRDialog::ClearHistory()
{
	persistHistory({});
	sessionHistory.clear();
	historyCount = 0;
	comboHost.ResetContent();
	comboHost.AddString(localized(IDS_STRING_CLEAR_HISTORY));
	comboHost.SetWindowTextW(L"");
}

void WinMTRDialog::AddHostToHistory(const std::wstring& host)
{
	sessionHistory.erase(std::remove_if(sessionHistory.begin(), sessionHistory.end(), [&](const std::wstring& value) {
		return _wcsicmp(value.c_str(), host.c_str()) == 0;
	}), sessionHistory.end());
	sessionHistory.emplace_back(host);
	keepNewest(sessionHistory, std::max(historyLimit, persistentHistoryLimit));
	auto storedHosts = sessionHistory;
	keepNewest(storedHosts, persistentHistoryLimit);
	persistHistory(storedHosts);
	auto visibleHosts = sessionHistory;
	keepNewest(visibleHosts, historyLimit);
	comboHost.ResetContent();
	for (const auto& value : visibleHosts) comboHost.AddString(value.c_str());
	comboHost.AddString(localized(IDS_STRING_CLEAR_HISTORY));
	comboHost.SetWindowTextW(host.c_str());
	historyCount = static_cast<int>(visibleHosts.size());
}

void WinMTRDialog::OnRestart() noexcept
{
	if (state == STATES::STOPPING || state == STATES::EXIT) return;
	if (state == STATES::TRACING) {
		Transit(STATES::STOPPING);
		return;
	}
	if (comboHost.GetCount() > 0 && comboHost.GetCurSel() == comboHost.GetCount() - 1) {
		ClearHistory();
		return;
	}
	if (!InitMTRNet()) return;
	CString host;
	comboHost.GetWindowTextW(host);
	host.Trim();
	currentTarget = host.GetString();
	AddHostToHistory(currentTarget);
	wmtrnet->ResetHops();
	listMtr.DeleteAllItems();
	displayRows.clear();
	lastAutoRowCount = 0;
	listMtr.ShowWindow(SW_HIDE);
	listIsVisible = false;
	firstDataResize = true;
	const CString resolvingFormat = localized(IDS_STATUS_RESOLVING_HOST);
	CString resolving;
	resolving.Format(resolvingFormat.GetString(), currentTarget.c_str());
	setStatus(resolving.GetString());
	Transit(STATES::TRACING);
}

void WinMTRDialog::OnOptions()
{
	if (state != STATES::IDLE) return;
	WinMTROptions options(this);
	options.SetInterval(interval.load());
	options.SetPingSize(packetSize.load());
	options.SetMaxHops(maxHops.load());
	options.SetTimeoutMs(timeoutMs.load());
	options.SetCycles(cycles.load());
	options.SetTos(tos.load());
	options.SetPattern(payloadPattern.load());
	options.SetMaxLRU(historyLimit);
	options.SetStartTtl(startTtl.load());
	options.SetMinimumTtl(minimumTtl.load());
	options.SetUnknownLimit(unknownHostLimit.load());
	options.SetEcmpDisplayLimit(ecmpDisplayLimit.load());
	options.SetReplyCacheSeconds(replyCacheSeconds.load());
	options.SetUseDNS(resolveNames.load());
	options.SetLookupAsnIsp(lookupAsnIsp.load());
	options.SetDontFragment(dontFragment.load());
	options.SetUseIPv4(useIPv4.load());
	options.SetUseIPv6(useIPv6.load());
	options.SetQueryPublicInfoOnStartup(queryPublicInfo.load());
	if (options.DoModal() != IDOK) return;

	const bool previouslyQuerying = queryPublicInfo.load();
	interval = options.GetInterval();
	packetSize = options.GetPingSize();
	maxHops = options.GetMaxHops();
	timeoutMs = options.GetTimeoutMs();
	cycles = options.GetCycles();
	tos = options.GetTos();
	payloadPattern = options.GetPattern();
	historyLimit = options.GetMaxLRU();
	persistentHistoryLimit = historyLimit;
	hasHistoryLimitFromCommandLine = false;
	startTtl = options.GetStartTtl();
	minimumTtl = options.GetMinimumTtl();
	unknownHostLimit = options.GetUnknownLimit();
	ecmpDisplayLimit = options.GetEcmpDisplayLimit();
	replyCacheSeconds = options.GetReplyCacheSeconds();
	resolveNames = options.GetUseDNS();
	lookupAsnIsp = options.GetLookupAsnIsp();
	dontFragment = options.GetDontFragment();
	useIPv4 = options.GetUseIPv4();
	useIPv6 = options.GetUseIPv6();
	queryPublicInfo = options.GetQueryPublicInfoOnStartup();
	SaveSettings();

	auto hosts = sessionHistory;
	keepNewest(hosts, historyLimit);
	sessionHistory = hosts;
	comboHost.ResetContent();
	for (const auto& value : hosts) comboHost.AddString(value.c_str());
	comboHost.AddString(localized(IDS_STRING_CLEAR_HISTORY));
	historyCount = static_cast<int>(hosts.size());
	persistHistory(hosts);

	if (!previouslyQuerying && queryPublicInfo.load()) startNetworkInfoQuery();
	else if (previouslyQuerying && !queryPublicInfo.load()) {
		networkInfoRestartPending = false;
		stopNetworkInfoQuery();
		{
			std::scoped_lock lock(networkInfoMutex);
			networkInfo = {};
		}
		buttonNetworkDetails.EnableWindow(FALSE);
		SetDlgItemTextW(IDC_STATIC_PUBLIC_IP, localized(IDS_PUBLIC_INFO_QUERY_FAILED));
		for (const int id : { IDC_STATIC_PUBLIC_HOSTNAME, IDC_STATIC_PUBLIC_COUNTRY, IDC_STATIC_PUBLIC_CITY,
			IDC_STATIC_PUBLIC_ASN, IDC_STATIC_PUBLIC_ISP }) {
			SetDlgItemTextW(id, localized(IDS_VALUE_UNAVAILABLE));
		}
	}
}
