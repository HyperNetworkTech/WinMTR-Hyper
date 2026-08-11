/*
WinMTR
Copyright (C) 2010-2019 Appnor MSP S.A.
Copyright (C) 2019-2025 Leetsoftwerx

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.
*/

module;
#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <afxwin.h>
#include "resource.h"
#include "WinMTRBranding.h"

export module WinMTR.Options;

import <algorithm>;
import <cmath>;
import <cwchar>;
import <format>;
import <limits>;
import <string>;

import WinMTR.License;
import WinMTRUtils;

export class WinMTROptions final : public CDialog
{
public:
	// Public aliases for the centralized product defaults used by the trace
	// engine, command-line parser and this dialog.
	static constexpr double DefaultInterval = WinMTRUtils::DEFAULT_INTERVAL;
	static constexpr unsigned DefaultPacketSize = WinMTRUtils::DEFAULT_PING_SIZE;
	static constexpr unsigned DefaultMaxHops = WinMTRUtils::DEFAULT_MAX_HOPS;
	static constexpr unsigned DefaultTimeoutMs = WinMTRUtils::DEFAULT_TIMEOUT_MS;
	static constexpr unsigned DefaultCycles = WinMTRUtils::DEFAULT_CYCLES;
	static constexpr unsigned DefaultTos = WinMTRUtils::DEFAULT_TOS;
	static constexpr int DefaultPattern = WinMTRUtils::DEFAULT_PAYLOAD_PATTERN;
	static constexpr unsigned DefaultHistoryLimit = WinMTRUtils::DEFAULT_MAX_LRU;
	static constexpr unsigned DefaultStartTtl = WinMTRUtils::DEFAULT_START_TTL;
	static constexpr unsigned DefaultMinimumTtl = WinMTRUtils::DEFAULT_MINIMUM_TTL;
	static constexpr unsigned DefaultUnknownLimit = WinMTRUtils::DEFAULT_UNKNOWN_HOST_LIMIT;
	static constexpr unsigned DefaultEcmpDisplayLimit = WinMTRUtils::DEFAULT_ECMP_DISPLAY_LIMIT;
	static constexpr unsigned DefaultReplyCacheSeconds = WinMTRUtils::DEFAULT_REPLY_CACHE_SECONDS;
	static constexpr bool DefaultResolveNames = WinMTRUtils::DEFAULT_USE_DNS;
	static constexpr bool DefaultLookupAsnIsp = WinMTRUtils::DEFAULT_LOOKUP_ASN_ISP;
	static constexpr bool DefaultDontFragment = WinMTRUtils::DEFAULT_DONT_FRAGMENT;
	static constexpr bool DefaultUseIPv4 = WinMTRUtils::DEFAULT_USE_IPV4;
	static constexpr bool DefaultUseIPv6 = WinMTRUtils::DEFAULT_USE_IPV6;
	static constexpr bool DefaultQueryPublicInfo = WinMTRUtils::DEFAULT_QUERY_PUBLIC_NETWORK_INFO;

	explicit WinMTROptions(CWnd* pParent = nullptr);

	void SetInterval(double value) noexcept { interval = value; }
	void SetPingSize(unsigned value) noexcept { packetSize = value; }
	void SetMaxHops(unsigned value) noexcept { maxHops = value; }
	void SetTimeoutMs(unsigned value) noexcept { timeoutMs = value; }
	void SetCycles(unsigned value) noexcept { cycles = value; }
	void SetTos(unsigned value) noexcept { tos = value; }
	void SetPattern(int value) noexcept { pattern = value; }
	void SetPayloadPattern(int value) noexcept { SetPattern(value); }
	void SetMaxLRU(unsigned value) noexcept { historyLimit = value; }
	void SetHistoryLimit(unsigned value) noexcept { SetMaxLRU(value); }
	void SetStartTtl(unsigned value) noexcept { startTtl = value; }
	void SetMinimumTtl(unsigned value) noexcept { minimumTtl = value; }
	void SetUnknownLimit(unsigned value) noexcept { unknownLimit = value; }
	void SetEcmpDisplayLimit(unsigned value) noexcept { ecmpDisplayLimit = value; }
	void SetReplyCacheSeconds(unsigned value) noexcept { replyCacheSeconds = value; }
	void SetUseDNS(bool value) noexcept { resolveNames = value; }
	void SetResolveNames(bool value) noexcept { SetUseDNS(value); }
	void SetLookupAsnIsp(bool value) noexcept { lookupAsnIsp = value; }
	void SetDontFragment(bool value) noexcept { dontFragment = value; }
	void SetUseIPv4(bool value) noexcept { useIPv4 = value; }
	void SetUseIPv6(bool value) noexcept { useIPv6 = value; }
	void SetQueryPublicInfoOnStartup(bool value) noexcept { queryPublicInfo = value; }
	void SetQueryPublicNetworkInfo(bool value) noexcept { SetQueryPublicInfoOnStartup(value); }

	[[nodiscard]] double GetInterval() const noexcept { return interval; }
	[[nodiscard]] unsigned GetPingSize() const noexcept { return packetSize; }
	[[nodiscard]] unsigned GetMaxHops() const noexcept { return maxHops; }
	[[nodiscard]] unsigned GetTimeoutMs() const noexcept { return timeoutMs; }
	[[nodiscard]] unsigned GetCycles() const noexcept { return cycles; }
	[[nodiscard]] unsigned GetTos() const noexcept { return tos; }
	[[nodiscard]] int GetPattern() const noexcept { return pattern; }
	[[nodiscard]] int GetPayloadPattern() const noexcept { return GetPattern(); }
	[[nodiscard]] unsigned GetMaxLRU() const noexcept { return historyLimit; }
	[[nodiscard]] unsigned GetHistoryLimit() const noexcept { return GetMaxLRU(); }
	[[nodiscard]] unsigned GetStartTtl() const noexcept { return startTtl; }
	[[nodiscard]] unsigned GetMinimumTtl() const noexcept { return minimumTtl; }
	[[nodiscard]] unsigned GetUnknownLimit() const noexcept { return unknownLimit; }
	[[nodiscard]] unsigned GetEcmpDisplayLimit() const noexcept { return ecmpDisplayLimit; }
	[[nodiscard]] unsigned GetReplyCacheSeconds() const noexcept { return replyCacheSeconds; }
	[[nodiscard]] bool GetUseDNS() const noexcept { return resolveNames; }
	[[nodiscard]] bool GetResolveNames() const noexcept { return GetUseDNS(); }
	[[nodiscard]] bool GetLookupAsnIsp() const noexcept { return lookupAsnIsp; }
	[[nodiscard]] bool GetDontFragment() const noexcept { return dontFragment; }
	[[nodiscard]] bool GetUseIPv4() const noexcept { return useIPv4; }
	[[nodiscard]] bool GetUseIPv6() const noexcept { return useIPv6; }
	[[nodiscard]] bool GetQueryPublicInfoOnStartup() const noexcept { return queryPublicInfo; }
	[[nodiscard]] bool GetQueryPublicNetworkInfo() const noexcept { return GetQueryPublicInfoOnStartup(); }

	enum { IDD = IDD_DIALOG_OPTIONS };

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;

	afx_msg void OnLicense();
	afx_msg void OnRestoreDefaults();
	afx_msg void OnVScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar);
	afx_msg void OnHScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar);
	afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	static constexpr double MinInterval = WinMTRUtils::MIN_INTERVAL;
	static constexpr double MaxInterval = WinMTRUtils::MAX_INTERVAL;
	static constexpr unsigned MinPacketSize = WinMTRUtils::MIN_PING_SIZE;
	static constexpr unsigned MaxPacketSize = WinMTRUtils::MAX_PING_SIZE;
	static constexpr unsigned MinMaxHops = WinMTRUtils::MIN_MAX_HOPS;
	static constexpr unsigned MaxMaxHops = WinMTRUtils::MAX_MAX_HOPS;
	static constexpr unsigned MinTimeoutMs = WinMTRUtils::MIN_TIMEOUT_MS;
	static constexpr unsigned MaxTimeoutMs = WinMTRUtils::MAX_TIMEOUT_MS;
	static constexpr unsigned MinCycles = WinMTRUtils::MIN_CYCLES;
	static constexpr unsigned MaxCycles = WinMTRUtils::MAX_CYCLES;
	static constexpr unsigned MinTos = WinMTRUtils::MIN_TOS;
	static constexpr unsigned MaxTos = WinMTRUtils::MAX_TOS;
	static constexpr int MinPattern = WinMTRUtils::MIN_PAYLOAD_PATTERN;
	static constexpr int MaxPattern = WinMTRUtils::MAX_PAYLOAD_PATTERN;
	static constexpr unsigned MinHistoryLimit = WinMTRUtils::MIN_MAX_LRU;
	static constexpr unsigned MaxHistoryLimit = WinMTRUtils::MAX_MAX_LRU;
	static constexpr unsigned MinStartTtl = WinMTRUtils::MIN_START_TTL;
	static constexpr unsigned MinMinimumTtl = WinMTRUtils::MIN_MINIMUM_TTL;
	static constexpr unsigned MinUnknownLimit = WinMTRUtils::MIN_UNKNOWN_HOST_LIMIT;
	static constexpr unsigned MaxUnknownLimit = WinMTRUtils::MAX_UNKNOWN_HOST_LIMIT;
	static constexpr unsigned MinEcmpDisplayLimit = WinMTRUtils::MIN_ECMP_DISPLAY_LIMIT;
	static constexpr unsigned MaxEcmpDisplayLimit = WinMTRUtils::MAX_ECMP_RESPONDERS;
	static constexpr unsigned MinReplyCacheSeconds = WinMTRUtils::MIN_REPLY_CACHE_SECONDS;
	static constexpr unsigned MaxReplyCacheSeconds = WinMTRUtils::MAX_REPLY_CACHE_SECONDS;

	double interval = DefaultInterval;
	unsigned packetSize = DefaultPacketSize;
	unsigned maxHops = DefaultMaxHops;
	unsigned timeoutMs = DefaultTimeoutMs;
	unsigned cycles = DefaultCycles;
	unsigned tos = DefaultTos;
	int pattern = DefaultPattern;
	unsigned historyLimit = DefaultHistoryLimit;
	unsigned startTtl = DefaultStartTtl;
	unsigned minimumTtl = DefaultMinimumTtl;
	unsigned unknownLimit = DefaultUnknownLimit;
	unsigned ecmpDisplayLimit = DefaultEcmpDisplayLimit;
	unsigned replyCacheSeconds = DefaultReplyCacheSeconds;
	bool resolveNames = DefaultResolveNames;
	bool lookupAsnIsp = DefaultLookupAsnIsp;
	bool dontFragment = DefaultDontFragment;
	bool useIPv4 = DefaultUseIPv4;
	bool useIPv6 = DefaultUseIPv6;
	bool queryPublicInfo = DefaultQueryPublicInfo;

	CEdit editInterval;
	CEdit editPacketSize;
	CEdit editMaxHops;
	CEdit editTimeoutMs;
	CEdit editCycles;
	CEdit editTos;
	CEdit editPattern;
	CEdit editHistoryLimit;
	CEdit editStartTtl;
	CEdit editMinimumTtl;
	CEdit editUnknownLimit;
	CEdit editEcmpDisplayLimit;
	CEdit editReplyCacheSeconds;
	CButton checkResolveNames;
	CButton checkLookupAsnIsp;
	CButton checkDontFragment;
	CButton checkIPv4;
	CButton checkIPv6;
	CButton checkQueryPublicInfo;
	CFont technicalFont;
	int scrollPosition = 0;
	int scrollMaximum = 0;
	int horizontalScrollPosition = 0;
	int horizontalScrollMaximum = 0;

	void RestoreDefaultValues() noexcept;
	void PopulateControls();
	void ApplyTechnicalFont();
	void ShowRangeError();
	void ConfigureResponsiveLayout();
	void MoveControlDlu(int id, int x, int y, int width, int height);
	void ConfigureVerticalScrolling();
	void ScrollTo(int position);
	void ScrollToHorizontal(int position);
};

module : private;

namespace
{
	void SetUnsignedText(CEdit& edit, unsigned value)
	{
		const auto text = std::format(L"{}", value);
		edit.SetWindowTextW(text.c_str());
	}

	void SetSignedText(CEdit& edit, int value)
	{
		const auto text = std::format(L"{}", value);
		edit.SetWindowTextW(text.c_str());
	}

	bool ParseDouble(CEdit& edit, double& value)
	{
		CString text;
		edit.GetWindowTextW(text);
		text.Trim();
		if (text.IsEmpty()) {
			return false;
		}

		wchar_t* end = nullptr;
		const auto parsed = std::wcstod(text.GetString(), &end);
		if (end == text.GetString() || *end != L'\0' || !std::isfinite(parsed)) {
			return false;
		}
		value = parsed;
		return true;
	}

	bool ParseUnsigned(CEdit& edit, unsigned& value)
	{
		CString text;
		edit.GetWindowTextW(text);
		text.Trim();
		if (text.IsEmpty() || text[0] == L'-') {
			return false;
		}

		wchar_t* end = nullptr;
		const auto parsed = std::wcstoull(text.GetString(), &end, 10);
		if (end == text.GetString() || *end != L'\0' ||
			parsed > std::numeric_limits<unsigned>::max()) {
			return false;
		}
		value = static_cast<unsigned>(parsed);
		return true;
	}

	bool ParseSigned(CEdit& edit, int& value)
	{
		CString text;
		edit.GetWindowTextW(text);
		text.Trim();
		if (text.IsEmpty()) {
			return false;
		}

		wchar_t* end = nullptr;
		const auto parsed = std::wcstoll(text.GetString(), &end, 10);
		if (end == text.GetString() || *end != L'\0' ||
			parsed < std::numeric_limits<int>::min() ||
			parsed > std::numeric_limits<int>::max()) {
			return false;
		}
		value = static_cast<int>(parsed);
		return true;
	}

	bool InRange(double value, double minimum, double maximum) noexcept
	{
		return value >= minimum && value <= maximum;
	}

	template <typename T>
	bool InRange(T value, T minimum, T maximum) noexcept
	{
		return value >= minimum && value <= maximum;
	}
}

BEGIN_MESSAGE_MAP(WinMTROptions, CDialog)
	ON_BN_CLICKED(IDC_BUTTON_LICENSE, &WinMTROptions::OnLicense)
	ON_BN_CLICKED(IDC_BUTTON_RESTORE_DEFAULTS, &WinMTROptions::OnRestoreDefaults)
	ON_WM_VSCROLL()
	ON_WM_HSCROLL()
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

WinMTROptions::WinMTROptions(CWnd* pParent)
	: CDialog(WinMTROptions::IDD, pParent)
{
}

void WinMTROptions::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_INTERVAL, editInterval);
	DDX_Control(pDX, IDC_EDIT_PACKET_SIZE, editPacketSize);
	DDX_Control(pDX, IDC_EDIT_MAX_HOPS, editMaxHops);
	DDX_Control(pDX, IDC_EDIT_TIMEOUT_MS, editTimeoutMs);
	DDX_Control(pDX, IDC_EDIT_CYCLES, editCycles);
	DDX_Control(pDX, IDC_EDIT_TOS, editTos);
	DDX_Control(pDX, IDC_EDIT_PATTERN, editPattern);
	DDX_Control(pDX, IDC_EDIT_HISTORY_LIMIT, editHistoryLimit);
	DDX_Control(pDX, IDC_EDIT_START_TTL, editStartTtl);
	DDX_Control(pDX, IDC_EDIT_MIN_TTL, editMinimumTtl);
	DDX_Control(pDX, IDC_EDIT_UNKNOWN_LIMIT, editUnknownLimit);
	DDX_Control(pDX, IDC_EDIT_ECMP_LIMIT, editEcmpDisplayLimit);
	DDX_Control(pDX, IDC_EDIT_REPLY_CACHE_SECONDS, editReplyCacheSeconds);
	DDX_Control(pDX, IDC_CHECK_RESOLVE_NAMES, checkResolveNames);
	DDX_Control(pDX, IDC_CHECK_LOOKUP_ASN_ISP, checkLookupAsnIsp);
	DDX_Control(pDX, IDC_CHECK_DONT_FRAGMENT, checkDontFragment);
	DDX_Control(pDX, IDC_CHECK_IPV4, checkIPv4);
	DDX_Control(pDX, IDC_CHECK_IPV6, checkIPv6);
	DDX_Control(pDX, IDC_CHECK_QUERY_PUBLIC_INFO, checkQueryPublicInfo);
}

BOOL WinMTROptions::OnInitDialog()
{
	CDialog::OnInitDialog();
	PopulateControls();
	ApplyTechnicalFont();
	ConfigureResponsiveLayout();
	ConfigureVerticalScrolling();
	editInterval.SetFocus();
	editInterval.SetSel(0, -1);
	return FALSE;
}

void WinMTROptions::PopulateControls()
{
	const auto intervalText = std::format(L"{:.1f}", interval);
	editInterval.SetWindowTextW(intervalText.c_str());
	SetUnsignedText(editPacketSize, packetSize);
	SetUnsignedText(editMaxHops, maxHops);
	SetUnsignedText(editTimeoutMs, timeoutMs);
	SetUnsignedText(editCycles, cycles);
	SetUnsignedText(editTos, tos);
	SetSignedText(editPattern, pattern);
	SetUnsignedText(editHistoryLimit, historyLimit);
	SetUnsignedText(editStartTtl, startTtl);
	SetUnsignedText(editMinimumTtl, minimumTtl);
	SetUnsignedText(editUnknownLimit, unknownLimit);
	SetUnsignedText(editEcmpDisplayLimit, ecmpDisplayLimit);
	SetUnsignedText(editReplyCacheSeconds, replyCacheSeconds);

	checkResolveNames.SetCheck(resolveNames ? BST_CHECKED : BST_UNCHECKED);
	checkLookupAsnIsp.SetCheck(lookupAsnIsp ? BST_CHECKED : BST_UNCHECKED);
	checkDontFragment.SetCheck(dontFragment ? BST_CHECKED : BST_UNCHECKED);
	checkIPv4.SetCheck(useIPv4 ? BST_CHECKED : BST_UNCHECKED);
	checkIPv6.SetCheck(useIPv6 ? BST_CHECKED : BST_UNCHECKED);
	checkQueryPublicInfo.SetCheck(queryPublicInfo ? BST_CHECKED : BST_UNCHECKED);
}

void WinMTROptions::ApplyTechnicalFont()
{
	if (!technicalFont.GetSafeHandle() &&
		technicalFont.CreatePointFont(90, WinMTRBranding::table_font.data())) {
		CEdit* technicalEdits[] = {
			&editInterval, &editPacketSize, &editMaxHops, &editTimeoutMs,
			&editCycles, &editTos, &editPattern, &editHistoryLimit,
			&editStartTtl, &editMinimumTtl, &editUnknownLimit,
			&editEcmpDisplayLimit, &editReplyCacheSeconds
		};
		for (CEdit* edit : technicalEdits) {
			edit->SetFont(&technicalFont);
		}
	}
}

void WinMTROptions::MoveControlDlu(int id, int x, int y, int width, int height)
{
	CRect rect(x, y, x + width, y + height);
	MapDialogRect(&rect);
	if (auto* control = GetDlgItem(id)) {
		control->SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void WinMTROptions::ConfigureResponsiveLayout()
{
	CRect windowRect;
	GetWindowRect(windowRect);
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
	if (windowRect.Width() <= workWidth) return;

	// A narrow single-column layout keeps every field and button fully visible;
	// the existing vertical viewport handles the additional height.
	constexpr int clientWidthDlu = 260;
	constexpr int clientHeightDlu = 438;
	MoveControlDlu(IDC_OPTIONS_GROUP_BRAND, 7, 7, 246, 29);
	MoveControlDlu(IDC_OPTIONS_ICON, 15, 12, 18, 18);
	MoveControlDlu(IDC_OPTIONS_PRODUCT, 42, 14, 201, 14);
	MoveControlDlu(IDC_OPTIONS_GROUP_TRACE, 7, 41, 246, 228);

	struct FieldRow final { int label; int edit; };
	constexpr FieldRow rows[] = {
		{ IDC_OPTIONS_LABEL_INTERVAL, IDC_EDIT_INTERVAL },
		{ IDC_OPTIONS_LABEL_PACKET_SIZE, IDC_EDIT_PACKET_SIZE },
		{ IDC_OPTIONS_LABEL_MAX_HOPS, IDC_EDIT_MAX_HOPS },
		{ IDC_OPTIONS_LABEL_TIMEOUT, IDC_EDIT_TIMEOUT_MS },
		{ IDC_OPTIONS_LABEL_CYCLES, IDC_EDIT_CYCLES },
		{ IDC_OPTIONS_LABEL_TOS, IDC_EDIT_TOS },
		{ IDC_OPTIONS_LABEL_PATTERN, IDC_EDIT_PATTERN },
		{ IDC_OPTIONS_LABEL_HISTORY, IDC_EDIT_HISTORY_LIMIT },
		{ IDC_OPTIONS_LABEL_START_TTL, IDC_EDIT_START_TTL },
		{ IDC_OPTIONS_LABEL_MIN_TTL, IDC_EDIT_MIN_TTL },
		{ IDC_OPTIONS_LABEL_UNKNOWN, IDC_EDIT_UNKNOWN_LIMIT },
		{ IDC_OPTIONS_LABEL_ECMP, IDC_EDIT_ECMP_LIMIT },
		{ IDC_OPTIONS_LABEL_REPLY_CACHE, IDC_EDIT_REPLY_CACHE_SECONDS }
	};
	for (int index = 0; index < static_cast<int>(std::size(rows)); ++index) {
		const int labelY = 54 + index * 16;
		MoveControlDlu(rows[index].label, 15, labelY, 170, 11);
		MoveControlDlu(rows[index].edit, 191, labelY - 2, 52, 14);
	}

	MoveControlDlu(IDC_OPTIONS_GROUP_NETWORK, 7, 275, 246, 105);
	constexpr int checkIds[] = {
		IDC_CHECK_RESOLVE_NAMES, IDC_CHECK_LOOKUP_ASN_ISP,
		IDC_CHECK_DONT_FRAGMENT, IDC_CHECK_IPV4, IDC_CHECK_IPV6,
		IDC_CHECK_QUERY_PUBLIC_INFO
	};
	for (int index = 0; index < static_cast<int>(std::size(checkIds)); ++index) {
		MoveControlDlu(checkIds[index], 15, 288 + index * 15, 228, 12);
	}

	MoveControlDlu(IDC_BUTTON_LICENSE, 7, 388, 90, 18);
	MoveControlDlu(IDC_BUTTON_RESTORE_DEFAULTS, 103, 388, 75, 18);
	MoveControlDlu(IDOK, 127, 412, 56, 18);
	MoveControlDlu(IDCANCEL, 187, 412, 56, 18);

	CRect desiredClient(0, 0, clientWidthDlu, clientHeightDlu);
	MapDialogRect(&desiredClient);
	CRect oldClient;
	GetClientRect(oldClient);
	SetWindowPos(nullptr, 0, 0,
		desiredClient.Width() + windowRect.Width() - oldClient.Width(),
		desiredClient.Height() + windowRect.Height() - oldClient.Height(),
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WinMTROptions::ConfigureVerticalScrolling()
{
	ShowScrollBar(SB_VERT, FALSE);
	ShowScrollBar(SB_HORZ, FALSE);
	CRect originalClient;
	CRect windowRect;
	GetClientRect(originalClient);
	GetWindowRect(windowRect);
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
	const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
	const int newHeight = std::min(windowRect.Height(), workHeight);
	const int newWidth = std::min(windowRect.Width(), workWidth);
	const int maximumLeft = std::max(static_cast<int>(monitor.rcWork.left),
		static_cast<int>(monitor.rcWork.right) - newWidth);
	const int newLeft = std::clamp(static_cast<int>(windowRect.left),
		static_cast<int>(monitor.rcWork.left), maximumLeft);
	const int newTop = std::clamp(static_cast<int>(windowRect.top),
		static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - newHeight);
	SetWindowPos(nullptr, newLeft, newTop, newWidth, newHeight,
		SWP_NOZORDER | SWP_NOACTIVATE);

	CRect viewport;
	for (int pass = 0; pass < 2; ++pass) {
		GetClientRect(viewport);
		ShowScrollBar(SB_VERT, originalClient.Height() > viewport.Height());
		ShowScrollBar(SB_HORZ, originalClient.Width() > viewport.Width());
	}
	GetClientRect(viewport);
	scrollPosition = 0;
	horizontalScrollPosition = 0;
	scrollMaximum = std::max(0, originalClient.Height() - viewport.Height());
	horizontalScrollMaximum = std::max(0, originalClient.Width() - viewport.Width());
	if (scrollMaximum != 0) {
		SCROLLINFO info{
			.cbSize = sizeof(SCROLLINFO),
			.fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
			.nMin = 0,
			.nMax = originalClient.Height() - 1,
			.nPage = static_cast<UINT>(std::max(1, viewport.Height())),
			.nPos = 0
		};
		SetScrollInfo(SB_VERT, &info, TRUE);
	}
	if (horizontalScrollMaximum != 0) {
		SCROLLINFO info{
			.cbSize = sizeof(SCROLLINFO),
			.fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
			.nMin = 0,
			.nMax = originalClient.Width() - 1,
			.nPage = static_cast<UINT>(std::max(1, viewport.Width())),
			.nPos = 0
		};
		SetScrollInfo(SB_HORZ, &info, TRUE);
	}
}

void WinMTROptions::ScrollTo(int position)
{
	const int next = std::clamp(position, 0, scrollMaximum);
	const int delta = next - scrollPosition;
	if (delta == 0) return;
	for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow()) {
		CRect rect;
		child->GetWindowRect(rect);
		ScreenToClient(rect);
		child->SetWindowPos(nullptr, rect.left, rect.top - delta, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	scrollPosition = next;
	SetScrollPos(SB_VERT, scrollPosition, TRUE);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void WinMTROptions::ScrollToHorizontal(int position)
{
	const int next = std::clamp(position, 0, horizontalScrollMaximum);
	const int delta = next - horizontalScrollPosition;
	if (delta == 0) return;
	for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow()) {
		CRect rect;
		child->GetWindowRect(rect);
		ScreenToClient(rect);
		child->SetWindowPos(nullptr, rect.left - delta, rect.top, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	horizontalScrollPosition = next;
	SetScrollPos(SB_HORZ, horizontalScrollPosition, TRUE);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void WinMTROptions::OnVScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar)
{
	if (scrollBar != nullptr || scrollMaximum == 0) {
		CDialog::OnVScroll(scrollCode, position, scrollBar);
		return;
	}
	CRect client;
	GetClientRect(client);
	int next = scrollPosition;
	switch (scrollCode) {
	case SB_LINEUP: next -= 20; break;
	case SB_LINEDOWN: next += 20; break;
	case SB_PAGEUP: next -= std::max(20, client.Height() - 40); break;
	case SB_PAGEDOWN: next += std::max(20, client.Height() - 40); break;
	case SB_TOP: next = 0; break;
	case SB_BOTTOM: next = scrollMaximum; break;
	case SB_THUMBPOSITION:
	case SB_THUMBTRACK: {
		SCROLLINFO info{ .cbSize = sizeof(SCROLLINFO), .fMask = SIF_TRACKPOS };
		GetScrollInfo(SB_VERT, &info);
		next = info.nTrackPos;
		break;
	}
	default: break;
	}
	ScrollTo(next);
}

void WinMTROptions::OnHScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar)
{
	if (scrollBar != nullptr || horizontalScrollMaximum == 0) {
		CDialog::OnHScroll(scrollCode, position, scrollBar);
		return;
	}
	CRect client;
	GetClientRect(client);
	int next = horizontalScrollPosition;
	switch (scrollCode) {
	case SB_LINELEFT: next -= 20; break;
	case SB_LINERIGHT: next += 20; break;
	case SB_PAGELEFT: next -= std::max(20, client.Width() - 40); break;
	case SB_PAGERIGHT: next += std::max(20, client.Width() - 40); break;
	case SB_LEFT: next = 0; break;
	case SB_RIGHT: next = horizontalScrollMaximum; break;
	case SB_THUMBPOSITION:
	case SB_THUMBTRACK: {
		SCROLLINFO info{ .cbSize = sizeof(SCROLLINFO), .fMask = SIF_TRACKPOS };
		GetScrollInfo(SB_HORZ, &info);
		next = info.nTrackPos;
		break;
	}
	default: break;
	}
	ScrollToHorizontal(next);
}

BOOL WinMTROptions::OnMouseWheel(UINT flags, short delta, CPoint point)
{
	if (scrollMaximum == 0) return CDialog::OnMouseWheel(flags, delta, point);
	ScrollTo(scrollPosition - (delta / WHEEL_DELTA) * 60);
	return TRUE;
}

void WinMTROptions::RestoreDefaultValues() noexcept
{
	interval = DefaultInterval;
	packetSize = DefaultPacketSize;
	maxHops = DefaultMaxHops;
	timeoutMs = DefaultTimeoutMs;
	cycles = DefaultCycles;
	tos = DefaultTos;
	pattern = DefaultPattern;
	historyLimit = DefaultHistoryLimit;
	startTtl = DefaultStartTtl;
	minimumTtl = DefaultMinimumTtl;
	unknownLimit = DefaultUnknownLimit;
	ecmpDisplayLimit = DefaultEcmpDisplayLimit;
	replyCacheSeconds = DefaultReplyCacheSeconds;
	resolveNames = DefaultResolveNames;
	lookupAsnIsp = DefaultLookupAsnIsp;
	dontFragment = DefaultDontFragment;
	useIPv4 = DefaultUseIPv4;
	useIPv6 = DefaultUseIPv6;
	queryPublicInfo = DefaultQueryPublicInfo;
}

void WinMTROptions::OnRestoreDefaults()
{
	RestoreDefaultValues();
	PopulateControls();
	ScrollTo(0);
	editInterval.SetFocus();
	editInterval.SetSel(0, -1);
}

void WinMTROptions::ShowRangeError()
{
	AfxMessageBox(IDS_ERROR_OPTIONS_OUT_OF_RANGE, MB_OK | MB_ICONWARNING);
	editInterval.SetFocus();
	editInterval.SetSel(0, -1);
}

void WinMTROptions::OnOK()
{
	double parsedInterval = 0.0;
	unsigned parsedPacketSize = 0;
	unsigned parsedMaxHops = 0;
	unsigned parsedTimeoutMs = 0;
	unsigned parsedCycles = 0;
	unsigned parsedTos = 0;
	int parsedPattern = 0;
	unsigned parsedHistoryLimit = 0;
	unsigned parsedStartTtl = 0;
	unsigned parsedMinimumTtl = 0;
	unsigned parsedUnknownLimit = 0;
	unsigned parsedEcmpDisplayLimit = 0;
	unsigned parsedReplyCacheSeconds = 0;

	const bool valid =
		ParseDouble(editInterval, parsedInterval) &&
		ParseUnsigned(editPacketSize, parsedPacketSize) &&
		ParseUnsigned(editMaxHops, parsedMaxHops) &&
		ParseUnsigned(editTimeoutMs, parsedTimeoutMs) &&
		ParseUnsigned(editCycles, parsedCycles) &&
		ParseUnsigned(editTos, parsedTos) &&
		ParseSigned(editPattern, parsedPattern) &&
		ParseUnsigned(editHistoryLimit, parsedHistoryLimit) &&
		ParseUnsigned(editStartTtl, parsedStartTtl) &&
		ParseUnsigned(editMinimumTtl, parsedMinimumTtl) &&
		ParseUnsigned(editUnknownLimit, parsedUnknownLimit) &&
		ParseUnsigned(editEcmpDisplayLimit, parsedEcmpDisplayLimit) &&
		ParseUnsigned(editReplyCacheSeconds, parsedReplyCacheSeconds) &&
		InRange(parsedInterval, MinInterval, MaxInterval) &&
		InRange(parsedPacketSize, MinPacketSize, MaxPacketSize) &&
		InRange(parsedMaxHops, MinMaxHops, MaxMaxHops) &&
		InRange(parsedTimeoutMs, MinTimeoutMs, MaxTimeoutMs) &&
		InRange(parsedCycles, MinCycles, MaxCycles) &&
		InRange(parsedTos, MinTos, MaxTos) &&
		InRange(parsedPattern, MinPattern, MaxPattern) &&
		InRange(parsedHistoryLimit, MinHistoryLimit, MaxHistoryLimit) &&
		InRange(parsedStartTtl, MinStartTtl, parsedMaxHops) &&
		InRange(parsedMinimumTtl, MinMinimumTtl, parsedMaxHops) &&
		InRange(parsedUnknownLimit, MinUnknownLimit, MaxUnknownLimit) &&
		InRange(parsedEcmpDisplayLimit, MinEcmpDisplayLimit, MaxEcmpDisplayLimit) &&
		InRange(parsedReplyCacheSeconds, MinReplyCacheSeconds, MaxReplyCacheSeconds);

	if (!valid) {
		ShowRangeError();
		return;
	}

	const bool parsedUseIPv4 = checkIPv4.GetCheck() == BST_CHECKED;
	const bool parsedUseIPv6 = checkIPv6.GetCheck() == BST_CHECKED;
	if (!parsedUseIPv4 && !parsedUseIPv6) {
		AfxMessageBox(IDS_ERROR_SELECT_IP_VERSION, MB_OK | MB_ICONWARNING);
		checkIPv4.SetFocus();
		return;
	}

	interval = parsedInterval;
	packetSize = parsedPacketSize;
	maxHops = parsedMaxHops;
	timeoutMs = parsedTimeoutMs;
	cycles = parsedCycles;
	tos = parsedTos;
	pattern = parsedPattern;
	historyLimit = parsedHistoryLimit;
	startTtl = parsedStartTtl;
	minimumTtl = parsedMinimumTtl;
	unknownLimit = parsedUnknownLimit;
	ecmpDisplayLimit = parsedEcmpDisplayLimit;
	replyCacheSeconds = parsedReplyCacheSeconds;
	resolveNames = checkResolveNames.GetCheck() == BST_CHECKED;
	lookupAsnIsp = checkLookupAsnIsp.GetCheck() == BST_CHECKED;
	dontFragment = checkDontFragment.GetCheck() == BST_CHECKED;
	useIPv4 = parsedUseIPv4;
	useIPv6 = parsedUseIPv6;
	queryPublicInfo = checkQueryPublicInfo.GetCheck() == BST_CHECKED;

	CDialog::OnOK();
}

void WinMTROptions::OnLicense()
{
	WinMTRLicense licenseDialog(this);
	licenseDialog.DoModal();
}
