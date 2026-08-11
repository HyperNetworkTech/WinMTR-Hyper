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

module;
#pragma warning (disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN

#include <afxwin.h>
#include <afxext.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include "WinMTRBranding.h"

export module WinMTR.CommandLineParser;

import WinMTR.Dialog;

export namespace utils {

	class CWinMTRCommandLineParser final : public CCommandLineInfo
	{
	public:
		explicit CWinMTRCommandLineParser(WinMTRDialog& dlg) noexcept
			: dlg(dlg)
		{
		}

		[[nodiscard]] bool isAskingForHelp() const noexcept
		{
			return m_help;
		}
		[[nodiscard]] bool hasError() const noexcept { return m_error; }
		void Validate() noexcept;

	private:
		void ParseParam(const WCHAR* pszParam, BOOL bFlag, BOOL bLast) noexcept override final;
#ifdef _UNICODE
		void ParseParam(
			[[maybe_unused]] const char* pszParam,
			[[maybe_unused]] BOOL bFlag,
			[[maybe_unused]] BOOL bLast) noexcept override final
		{
		}
#endif

		enum class expect_next {
			none,
			interval,
			ping_size,
			lru,
			max_hops,
			timeout,
			cycles,
			tos,
			pattern,
			start_ttl,
			minimum_ttl,
			unknown_hosts,
			ecmp_limit,
			reply_cache,
			public_refresh_minutes
		};

		void ReportError(const wchar_t* prefix, const wchar_t* detail = nullptr) noexcept;
		void ReportInvalidValue(const wchar_t* option, const wchar_t* value) noexcept;
		[[nodiscard]] const wchar_t* PendingOptionName() const noexcept;

		WinMTRDialog& dlg;
		expect_next next = expect_next::none;
		bool m_help = false;
		bool m_error = false;
		bool m_has_target = false;
	};
}


module : private;

import WinMTRUtils;

namespace {
	[[nodiscard]] bool IsOption(
		const wchar_t* value,
		const wchar_t* short_name,
		const wchar_t* long_name) noexcept
	{
		if (_wcsicmp(value, short_name) == 0 || _wcsicmp(value, long_name) == 0) {
			return true;
		}

		// MFC removes one leading '-' or '/'. Consequently, --long reaches
		// ParseParam as -long, while -long and /long arrive as long.
		return value[0] == L'-' && _wcsicmp(value + 1, long_name) == 0;
	}

	[[nodiscard]] bool ParseInteger(const wchar_t* text, long& value) noexcept
	{
		errno = 0;
		wchar_t* end = nullptr;
		const auto parsed = std::wcstol(text, &end, 10);
		if (text == end || end == nullptr || *end != L'\0' || errno == ERANGE) {
			return false;
		}

		value = parsed;
		return true;
	}

	[[nodiscard]] bool ParseFloatingPoint(const wchar_t* text, double& value) noexcept
	{
		errno = 0;
		wchar_t* end = nullptr;
		const auto parsed = std::wcstod(text, &end);
		if (text == end || end == nullptr || *end != L'\0' || errno == ERANGE ||
			!std::isfinite(parsed)) {
			return false;
		}

		value = parsed;
		return true;
	}
}

void utils::CWinMTRCommandLineParser::ReportError(
	const wchar_t* prefix,
	const wchar_t* detail) noexcept
{
	try {
		std::wstring message(prefix);
		if (detail != nullptr) {
			message.append(detail);
		}
		std::fwprintf(stderr, L"%ls\n", message.c_str());
		std::fflush(stderr);
		AfxMessageBox(message.c_str(), MB_OK | MB_ICONERROR);
	}
	catch (...) {
		AfxMessageBox(
			WinMTRBranding::cli_strings::generic_error.data(),
			MB_OK | MB_ICONERROR);
	}

	m_error = true;
	m_help = true;
}

void utils::CWinMTRCommandLineParser::ReportInvalidValue(
	const wchar_t* option,
	const wchar_t* value) noexcept
{
	try {
		std::wstring detail(option);
		detail.append(L" = ");
		detail.append(value);
		ReportError(
			WinMTRBranding::cli_strings::invalid_value.data(),
			detail.c_str());
	}
	catch (...) {
		ReportError(WinMTRBranding::cli_strings::generic_error.data());
	}
}

const wchar_t* utils::CWinMTRCommandLineParser::PendingOptionName() const noexcept
{
	switch (next) {
	case expect_next::interval:
		return L"--interval / -i";
	case expect_next::ping_size:
		return L"--size / -s";
	case expect_next::lru:
		return L"--maxLRU / -m";
	case expect_next::max_hops: return L"--max-hops";
	case expect_next::timeout: return L"--timeout";
	case expect_next::cycles: return L"--cycles";
	case expect_next::tos: return L"--tos";
	case expect_next::pattern: return L"--pattern";
	case expect_next::start_ttl: return L"--start-ttl";
	case expect_next::minimum_ttl: return L"--minimum-ttl";
	case expect_next::unknown_hosts: return L"--unknown-hosts";
	case expect_next::ecmp_limit: return L"--ecmp-limit";
	case expect_next::reply_cache: return L"--cache";
	case expect_next::public_refresh_minutes: return L"--public-refresh-minutes";
	case expect_next::none:
	default:
		return L"";
	}
}

void utils::CWinMTRCommandLineParser::ParseParam(
	const WCHAR* pszParam,
	BOOL bFlag,
	BOOL bLast) noexcept
{
	if (pszParam == nullptr) {
		ReportError(WinMTRBranding::cli_strings::generic_error.data());
		return;
	}

	if (bFlag) {
		if (next != expect_next::none) {
			ReportError(
				WinMTRBranding::cli_strings::missing_value.data(),
				PendingOptionName());
			next = expect_next::none;
		}

		if (IsOption(pszParam, L"h", L"help")) {
			m_help = true;
			return;
		}
		if (IsOption(pszParam, L"n", L"numeric")) {
			dlg.SetUseDNS(false, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"resolve")) {
			dlg.SetUseDNS(true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"lookup-asn")) {
			dlg.SetLookupAsnIsp(true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"no-lookup-asn")) {
			dlg.SetLookupAsnIsp(false, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"dont-fragment")) {
			dlg.SetDontFragment(true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"allow-fragment")) {
			dlg.SetDontFragment(false, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"ipv4-only")) {
			dlg.SetAddressFamilies(true, false, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"ipv6-only")) {
			dlg.SetAddressFamilies(false, true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"dual-stack")) {
			dlg.SetAddressFamilies(true, true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"public-info")) {
			dlg.SetQueryPublicInfo(true, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"no-public-info")) {
			dlg.SetQueryPublicInfo(false, WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"", L"public-refresh-on-change")) {
			dlg.SetPublicInfoRefresh(WinMTRUtils::PUBLIC_INFO_REFRESH_ON_NETWORK_CHANGE,
				WinMTRUtils::DEFAULT_PUBLIC_INFO_REFRESH_MINUTES,
				WinMTRDialog::options_source::cmd_line);
			return;
		}
		if (IsOption(pszParam, L"i", L"interval")) {
			next = expect_next::interval;
		}
		else if (IsOption(pszParam, L"m", L"maxLRU")) {
			next = expect_next::lru;
		}
		else if (IsOption(pszParam, L"s", L"size")) {
			next = expect_next::ping_size;
		}
		else if (IsOption(pszParam, L"", L"max-hops")) next = expect_next::max_hops;
		else if (IsOption(pszParam, L"", L"timeout")) next = expect_next::timeout;
		else if (IsOption(pszParam, L"", L"cycles")) next = expect_next::cycles;
		else if (IsOption(pszParam, L"", L"tos")) next = expect_next::tos;
		else if (IsOption(pszParam, L"", L"pattern")) next = expect_next::pattern;
		else if (IsOption(pszParam, L"", L"start-ttl")) next = expect_next::start_ttl;
		else if (IsOption(pszParam, L"", L"minimum-ttl")) next = expect_next::minimum_ttl;
		else if (IsOption(pszParam, L"", L"unknown-hosts")) next = expect_next::unknown_hosts;
		else if (IsOption(pszParam, L"", L"ecmp-limit")) next = expect_next::ecmp_limit;
		else if (IsOption(pszParam, L"", L"cache")) next = expect_next::reply_cache;
		else if (IsOption(pszParam, L"", L"public-refresh-minutes")) {
			next = expect_next::public_refresh_minutes;
		}
		else {
			try {
				std::wstring shown_option(L"-");
				shown_option.append(pszParam);
				ReportError(
					WinMTRBranding::cli_strings::unknown_option.data(),
					shown_option.c_str());
			}
			catch (...) {
				ReportError(WinMTRBranding::cli_strings::generic_error.data());
			}
			return;
		}

		if (bLast) {
			ReportError(
				WinMTRBranding::cli_strings::missing_value.data(),
				PendingOptionName());
			next = expect_next::none;
		}
		return;
	}

	if (next == expect_next::none) {
		if (*pszParam == L'\0') {
			ReportError(WinMTRBranding::cli_strings::empty_target.data());
			return;
		}
		if (m_has_target) {
			ReportError(WinMTRBranding::cli_strings::multiple_targets.data());
			return;
		}

		// CWinApp::ParseCommandLine supplies the Unicode token after Windows has
		// processed quoting, so spaces and non-ASCII host names remain intact.
		dlg.SetHostName(std::wstring(pszParam));
		m_has_target = true;
		return;
	}

	const auto pending = next;
	const auto option_name = PendingOptionName();
	next = expect_next::none;
	const auto parseRangedInteger = [&](long minimum, long maximum, long& parsed) {
		if (!ParseInteger(pszParam, parsed) || parsed < minimum || parsed > maximum) {
			ReportInvalidValue(option_name, pszParam);
			return false;
		}
		return true;
	};

	switch (pending) {
	case expect_next::lru:
	{
		long parsed = 0;
		if (!ParseInteger(pszParam, parsed) ||
			parsed < static_cast<long>(WinMTRUtils::MIN_MAX_LRU) ||
			parsed > static_cast<long>(WinMTRUtils::MAX_MAX_LRU)) {
			ReportInvalidValue(option_name, pszParam);
			return;
		}
		dlg.SetMaxLRU(
			static_cast<int>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::interval:
	{
		double parsed = 0.0;
		if (!ParseFloatingPoint(pszParam, parsed) ||
			parsed < WinMTRUtils::MIN_INTERVAL ||
			parsed > WinMTRUtils::MAX_INTERVAL) {
			ReportInvalidValue(option_name, pszParam);
			return;
		}
		dlg.SetInterval(
			static_cast<float>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::ping_size:
	{
		long parsed = 0;
		if (!ParseInteger(pszParam, parsed) ||
			parsed < static_cast<long>(WinMTRUtils::MIN_PING_SIZE) ||
			parsed > static_cast<long>(WinMTRUtils::MAX_PING_SIZE)) {
			ReportInvalidValue(option_name, pszParam);
			return;
		}
		dlg.SetPingSize(
			static_cast<unsigned>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::max_hops:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_MAX_HOPS,
			WinMTRUtils::MAX_MAX_HOPS, parsed)) return;
		dlg.SetMaxHops(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::timeout:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_TIMEOUT_MS,
			WinMTRUtils::MAX_TIMEOUT_MS, parsed)) return;
		dlg.SetTimeoutMs(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::cycles:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_CYCLES,
			WinMTRUtils::MAX_CYCLES, parsed)) return;
		dlg.SetCycles(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::tos:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_TOS,
			WinMTRUtils::MAX_TOS, parsed)) return;
		dlg.SetTos(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::pattern:
	{
		if (_wcsicmp(pszParam, L"random") == 0) {
			dlg.SetPayloadPattern(WinMTRUtils::MIN_PAYLOAD_PATTERN,
				WinMTRDialog::options_source::cmd_line);
			break;
		}
		long parsed = 0;
		if (!parseRangedInteger(0, WinMTRUtils::MAX_PAYLOAD_PATTERN, parsed)) return;
		dlg.SetPayloadPattern(static_cast<int>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::start_ttl:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_START_TTL,
			WinMTRUtils::MAX_MAX_HOPS, parsed)) return;
		dlg.SetStartTtl(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::minimum_ttl:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_MINIMUM_TTL,
			WinMTRUtils::MAX_MAX_HOPS, parsed)) return;
		dlg.SetMinimumTtl(static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::unknown_hosts:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_UNKNOWN_HOST_LIMIT,
			WinMTRUtils::MAX_UNKNOWN_HOST_LIMIT, parsed)) return;
		dlg.SetUnknownHostLimit(static_cast<unsigned>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::ecmp_limit:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_ECMP_DISPLAY_LIMIT,
			WinMTRUtils::MAX_ECMP_RESPONDERS, parsed)) return;
		dlg.SetEcmpDisplayLimit(static_cast<unsigned>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::reply_cache:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_REPLY_CACHE_SECONDS,
			WinMTRUtils::MAX_REPLY_CACHE_SECONDS, parsed)) return;
		dlg.SetReplyCacheSeconds(static_cast<unsigned>(parsed),
			WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::public_refresh_minutes:
	{
		long parsed = 0;
		if (!parseRangedInteger(WinMTRUtils::MIN_PUBLIC_INFO_REFRESH_MINUTES,
			WinMTRUtils::MAX_PUBLIC_INFO_REFRESH_MINUTES, parsed)) return;
		dlg.SetPublicInfoRefresh(WinMTRUtils::PUBLIC_INFO_REFRESH_FIXED_INTERVAL,
			static_cast<unsigned>(parsed), WinMTRDialog::options_source::cmd_line);
		break;
	}
	case expect_next::none:
	default:
		break;
	}
}

void utils::CWinMTRCommandLineParser::Validate() noexcept
{
	if (m_error) return;
	try {
		if (dlg.getStartTtl() > dlg.getMaxHops()) {
			ReportError(WinMTRBranding::cli_strings::invalid_value.data(),
				L"--start-ttl must not exceed --max-hops");
			return;
		}
		if (dlg.getMinimumTtl() > dlg.getMaxHops()) {
			ReportError(WinMTRBranding::cli_strings::invalid_value.data(),
				L"--minimum-ttl must not exceed --max-hops");
		}
	}
	catch (...) {
		ReportError(WinMTRBranding::cli_strings::generic_error.data());
	}
}
