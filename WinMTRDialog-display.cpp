module;

#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <afx.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcmn.h>
#include <afxlinkctrl.h>
#include <uxtheme.h>
#include "resource.h"
#include "WinMTRProperties.h"
#include "WinMTRBranding.h"
#include "WinMTRNetworkData.h"
#pragma comment(lib, "iphlpapi.lib")

module WinMTR.Dialog:display;

import :ClassDef;
import <algorithm>;
import <array>;
import <climits>;
import <cstring>;
import <format>;
import <mutex>;
import <numeric>;
import <optional>;
import <span>;
import <string>;
import <string_view>;
import <utility>;
import WinMTRIPUtils;
import WinMTRSNetHost;
import WinMTRUtils;

using namespace std::literals;

namespace {

[[nodiscard]] UINT effectiveDpi(HWND window) noexcept
{
	using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
	static const auto getDpiForWindow = []() noexcept -> GetDpiForWindowFunction {
		const HMODULE user32 = GetModuleHandleW(L"user32.dll");
		return user32 == nullptr ? nullptr : reinterpret_cast<GetDpiForWindowFunction>(
			GetProcAddress(user32, "GetDpiForWindow"));
	}();
	if (getDpiForWindow != nullptr) {
		if (const UINT dpi = getDpiForWindow(window); dpi != 0) return dpi;
	}

	using GetDpiForMonitorFunction = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
	static const auto getDpiForMonitor = []() noexcept -> GetDpiForMonitorFunction {
		// shcore.dll does not exist on Windows 7, so resolve it without adding a
		// static import and fall back to the system DPI below.
		const HMODULE shcore = LoadLibraryW(L"shcore.dll");
		return shcore == nullptr ? nullptr : reinterpret_cast<GetDpiForMonitorFunction>(
			GetProcAddress(shcore, "GetDpiForMonitor"));
	}();
	if (getDpiForMonitor != nullptr && window != nullptr) {
		UINT horizontal = 0;
		UINT vertical = 0;
		if (SUCCEEDED(getDpiForMonitor(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
			0, &horizontal, &vertical)) && horizontal != 0) return horizontal;
	}

	if (HDC dc = ::GetDC(window); dc != nullptr) {
		const auto dpi = static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX));
		::ReleaseDC(window, dc);
		return dpi == 0 ? 96u : dpi;
	}
	return 96;
}

void createTechnicalFont(CFont& font, UINT dpi, int pointTenths)
{
	if (font.GetSafeHandle() != nullptr) font.DeleteObject();
	font.CreateFontW(-MulDiv(pointTenths, static_cast<int>(dpi), 720), 0, 0, 0,
		FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
		WinMTRBranding::table_font.data());
}

[[nodiscard]] int scaled(HWND window, int value) noexcept
{
	return MulDiv(value, static_cast<int>(effectiveDpi(window)), 96);
}

void moveControl(CWnd* parent, int id, int x, int y, int width, int height)
{
	if (auto control = parent->GetDlgItem(id); control != nullptr && ::IsWindow(control->GetSafeHwnd())) {
		const int targetWidth = std::max(width, 1);
		const int targetHeight = std::max(height, 1);
		CRect current;
		control->GetWindowRect(current);
		parent->ScreenToClient(current);
		if (current.left == x && current.top == y
			&& current.Width() == targetWidth && current.Height() == targetHeight) return;
		control->SetWindowPos(nullptr, x, y, targetWidth, targetHeight,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
	}
}

[[nodiscard]] int controlTextWidth(CWnd* parent, int id)
{
	CWnd* control = parent->GetDlgItem(id);
	if (control == nullptr || !::IsWindow(control->GetSafeHwnd())) return 0;
	CString text;
	control->GetWindowTextW(text);
	CClientDC dc(control);
	CFont* previous = nullptr;
	if (CFont* font = control->GetFont(); font != nullptr) previous = dc.SelectObject(font);
	const int width = dc.GetTextExtent(text).cx;
	if (previous != nullptr) dc.SelectObject(previous);
	return width;
}

[[nodiscard]] int publicLabelWidth(CWnd* parent, int id)
{
	return controlTextWidth(parent, id) + scaled(parent->GetSafeHwnd(), 4);
}

[[nodiscard]] int naturalPublicInformationWidth(CWnd* parent)
{
	const HWND window = parent->GetSafeHwnd();
	const int gap = scaled(window, 8);
	const int padding = scaled(window, 8);
	const std::array<int, 4> row1LabelIds{
		IDC_STATIC_PUBLIC_IP_LABEL, IDC_STATIC_PUBLIC_COUNTRY_LABEL,
		IDC_STATIC_PUBLIC_CITY_LABEL, IDC_STATIC_PUBLIC_ASN_LABEL
	};
	const std::array<int, 4> row1Labels{
		publicLabelWidth(parent, row1LabelIds[0]), publicLabelWidth(parent, row1LabelIds[1]),
		publicLabelWidth(parent, row1LabelIds[2]), publicLabelWidth(parent, row1LabelIds[3])
	};
	const std::array<int, 4> row1Ids{
		IDC_STATIC_PUBLIC_IP, IDC_STATIC_PUBLIC_COUNTRY,
		IDC_STATIC_PUBLIC_CITY, IDC_STATIC_PUBLIC_ASN
	};
	const std::array<int, 4> row1MinimumValues{
		scaled(window, 40), scaled(window, 20), scaled(window, 30), scaled(window, 30)
	};
	int row1 = gap * 3;
	for (size_t index = 0; index < row1Ids.size(); ++index) {
		row1 += row1Labels[index] + std::max(row1MinimumValues[index],
			controlTextWidth(parent, row1Ids[index]) + padding);
	}
	const int hostnameLabelWidth = publicLabelWidth(parent, IDC_STATIC_PUBLIC_HOSTNAME_LABEL);
	const int ispLabelWidth = publicLabelWidth(parent, IDC_STATIC_PUBLIC_ISP_LABEL);
	const int row2 = hostnameLabelWidth + std::max(scaled(window, 40),
		controlTextWidth(parent, IDC_STATIC_PUBLIC_HOSTNAME) + padding)
		+ gap + ispLabelWidth + std::max(scaled(window, 60),
			controlTextWidth(parent, IDC_STATIC_PUBLIC_ISP) + padding);
	return std::max(row1, row2);
}

[[nodiscard]] CString loadString(UINT id)
{
	CString value;
	value.LoadStringW(id);
	return value;
}

template <typename... Args>
[[nodiscard]] std::wstring formatString(UINT id, Args&&... args)
{
	const CString pattern = loadString(id);
	return std::vformat(std::wstring_view(pattern.GetString()),
		std::make_wformat_args(args...));
}

[[nodiscard]] bool setUnicodeClipboard(HWND owner, const std::wstring& value) noexcept
{
	if (!OpenClipboard(owner)) return false;
	struct ClipboardCloser final { ~ClipboardCloser() { CloseClipboard(); } } closer;
	if (!EmptyClipboard()) return false;
	const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (memory == nullptr) return false;
	void* destination = GlobalLock(memory);
	if (destination == nullptr) {
		GlobalFree(memory);
		return false;
	}
	memcpy(destination, value.c_str(), bytes);
	GlobalUnlock(memory);
	if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
		GlobalFree(memory);
		return false;
	}
	return true;
}

class NetworkInfoDialog final : public CDialog {
public:
	explicit NetworkInfoDialog(std::wstring contents, CWnd* parent)
		: CDialog(IDD_DIALOG_NETWORK_INFO, parent), contents(std::move(contents)) {}

protected:
	void DoDataExchange(CDataExchange* exchange) override
	{
		CDialog::DoDataExchange(exchange);
		DDX_Control(exchange, IDC_EDIT_NETWORK_INFO, edit);
	}

	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		edit.SetWindowTextW(contents.c_str());
		createTechnicalFont(technicalFont, effectiveDpi(GetSafeHwnd()), 90);
		ApplyMixedFonts();

		CRect windowRect;
		GetWindowRect(windowRect);
		MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
		GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
		lineCount = 1 + static_cast<int>(std::count(contents.begin(), contents.end(), L'\n'));
		const int desiredClientHeight = scaled(GetSafeHwnd(), 62 + std::min(lineCount, 28) * 17);
		CRect client;
		GetClientRect(client);
		int maximumLineWidth = 0;
		{
			CClientDC dc(&edit);
			CFont* previousFont = dc.SelectObject(&technicalFont);
			const int tabPixels = scaled(GetSafeHwnd(), 180);
			std::wstring_view remaining = contents;
			while (!remaining.empty()) {
				const auto newline = remaining.find(L'\n');
				auto line = remaining.substr(0, newline);
				if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
				const auto tab = line.find(L'\t');
				const int lineWidth = tab == std::wstring_view::npos
					? static_cast<int>(dc.GetTextExtent(line.data(), static_cast<int>(line.size())).cx)
					: tabPixels + static_cast<int>(dc.GetTextExtent(line.data() + tab + 1,
						static_cast<int>(line.size() - tab - 1)).cx);
				maximumLineWidth = std::max(maximumLineWidth, lineWidth);
				if (newline == std::wstring_view::npos) break;
				remaining.remove_prefix(newline + 1);
			}
			if (previousFont != nullptr) dc.SelectObject(previousFont);
		}
		const int delta = desiredClientHeight - client.Height();
		const int maximumHeight = monitor.rcWork.bottom - monitor.rcWork.top;
		const int minimumHeight = std::min(maximumHeight, scaled(GetSafeHwnd(), 280));
		const int newHeight = std::clamp(windowRect.Height() + delta, minimumHeight, maximumHeight);
		const int frameWidth = windowRect.Width() - client.Width();
		const int maximumWidth = monitor.rcWork.right - monitor.rcWork.left;
		const int desiredClientWidth = std::max(client.Width(), maximumLineWidth + scaled(GetSafeHwnd(), 40));
		const int newWidth = std::min(maximumWidth, desiredClientWidth + frameWidth);
		const int newLeft = std::clamp(static_cast<int>(windowRect.left), static_cast<int>(monitor.rcWork.left),
			static_cast<int>(monitor.rcWork.right) - newWidth);
		const int newTop = std::clamp(static_cast<int>(windowRect.top), static_cast<int>(monitor.rcWork.top),
			static_cast<int>(monitor.rcWork.bottom) - newHeight);
		SetWindowPos(nullptr, newLeft, newTop, newWidth, newHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
		GetClientRect(client);
		initialized = true;
		layout(client.Width(), client.Height());
		edit.SetSel(0, 0);
		edit.HideSelection(TRUE, FALSE);
		if (CWnd* close = GetDlgItem(IDCANCEL); close != nullptr) close->SetFocus();
		return FALSE;
	}

	afx_msg void OnSize(UINT type, int width, int height)
	{
		CDialog::OnSize(type, width, height);
		if (initialized && type != SIZE_MINIMIZED) layout(width, height);
	}

	afx_msg void OnGetMinMaxInfo(MINMAXINFO* info)
	{
		CDialog::OnGetMinMaxInfo(info);
		MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
		GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
		info->ptMinTrackSize.x = std::min(static_cast<int>(monitor.rcWork.right - monitor.rcWork.left),
			scaled(GetSafeHwnd(), 420));
		info->ptMinTrackSize.y = std::min(static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top),
			scaled(GetSafeHwnd(), 280));
	}

	void ApplyMixedFonts()
	{
		CString displayed;
		edit.GetWindowTextW(displayed);
		const auto applyFormat = [this](int first, int last, std::wstring_view face,
			bool bold, std::optional<COLORREF> colour = std::nullopt) {
			CHARFORMAT2 format{};
			format.cbSize = sizeof(format);
			format.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_BOLD;
			format.dwEffects = bold ? CFE_BOLD : 0;
			format.yHeight = 180;
			format.bCharSet = DEFAULT_CHARSET;
			if (colour) {
				format.dwMask |= CFM_COLOR;
				format.crTextColor = *colour;
			}
			wcsncpy_s(format.szFaceName, face.data(), _TRUNCATE);
			edit.SetSel(first, last);
			edit.SetSelectionCharFormat(format);
		};

		applyFormat(0, displayed.GetLength(), WinMTRBranding::ui_font, false);
		PARAFORMAT2 paragraph{};
		paragraph.cbSize = sizeof(paragraph);
		paragraph.dwMask = PFM_TABSTOPS;
		paragraph.cTabCount = 1;
		paragraph.rgxTabs[0] = 2700; // 1.875 inches: clears the longest Chinese label.
		edit.SetSel(0, displayed.GetLength());
		edit.SendMessageW(EM_SETPARAFORMAT, SCF_SELECTION,
			reinterpret_cast<LPARAM>(&paragraph));

		for (int lineStart = 0; lineStart < displayed.GetLength();) {
			int lineEnd = displayed.Find(L'\n', lineStart);
			if (lineEnd < 0) lineEnd = displayed.GetLength();
			int contentEnd = lineEnd;
			if (contentEnd > lineStart && displayed[contentEnd - 1] == L'\r') --contentEnd;
			const int tab = displayed.Find(L'\t', lineStart);
			if (tab >= lineStart && tab < contentEnd) {
				applyFormat(tab + 1, contentEnd, WinMTRBranding::table_font, false,
					RGB(32, 72, 112));
			}
			else if (contentEnd > lineStart) {
				applyFormat(lineStart, contentEnd, WinMTRBranding::ui_font, true);
			}
			lineStart = lineEnd + 1;
		}
		edit.SetSel(0, 0);
		edit.SetModify(FALSE);
	}

	void layout(int width, int height)
	{
		const int margin = scaled(GetSafeHwnd(), 10);
		const int buttonHeight = scaled(GetSafeHwnd(), 26);
		const int editHeight = height - margin * 3 - buttonHeight;
		moveControl(this, IDC_EDIT_NETWORK_INFO, margin, margin, width - margin * 2, editHeight);
		moveControl(this, IDC_BUTTON_NETWORK_INFO_COPY, width - margin * 2 - scaled(GetSafeHwnd(), 130),
			height - margin - buttonHeight, scaled(GetSafeHwnd(), 62), buttonHeight);
		moveControl(this, IDCANCEL, width - margin - scaled(GetSafeHwnd(), 62),
			height - margin - buttonHeight, scaled(GetSafeHwnd(), 62), buttonHeight);
		edit.ShowScrollBar(SB_VERT, edit.GetLineCount() * scaled(GetSafeHwnd(), 17) > editHeight);
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	afx_msg void OnCopy()
	{
		if (!setUnicodeClipboard(GetSafeHwnd(), contents)) {
			AfxMessageBox(IDS_ERROR_CLIPBOARD, MB_OK | MB_ICONERROR);
		}
	}

	DECLARE_MESSAGE_MAP()

private:
	std::wstring contents;
	CRichEditCtrl edit;
	CFont technicalFont;
	bool initialized = false;
	int lineCount = 0;
};

BEGIN_MESSAGE_MAP(NetworkInfoDialog, CDialog)
	ON_BN_CLICKED(IDC_BUTTON_NETWORK_INFO_COPY, &NetworkInfoDialog::OnCopy)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

constexpr std::array<UINT, 14> columnStringIds{
	IDS_COLUMN_HOST, IDS_COLUMN_HOP, IDS_COLUMN_LOSS, IDS_COLUMN_SENT, IDS_COLUMN_RECEIVED,
	IDS_COLUMN_BEST, IDS_COLUMN_AVERAGE, IDS_COLUMN_WORST, IDS_COLUMN_LAST, IDS_COLUMN_JITTER,
	IDS_COLUMN_STDDEV, IDS_COLUMN_COUNTRY, IDS_COLUMN_ASN, IDS_COLUMN_ISP
};

} // namespace

BEGIN_MESSAGE_MAP(WinMTRDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_SIZING()
	ON_WM_GETMINMAXINFO()
	ON_MESSAGE(WM_DPICHANGED, &WinMTRDialog::OnDpiChanged)
	ON_WM_QUERYDRAGICON()
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_BUTTON_START_STOP, &WinMTRDialog::OnRestart)
	ON_BN_CLICKED(IDC_BUTTON_OPTIONS, &WinMTRDialog::OnOptions)
	ON_BN_CLICKED(IDC_BUTTON_RESET_STATS, &WinMTRDialog::OnResetStatistics)
	ON_BN_CLICKED(IDC_BUTTON_SCREENSHOT, &WinMTRDialog::OnScreenshot)
	ON_BN_CLICKED(IDC_BUTTON_COPY_EXPORT, &WinMTRDialog::OnCopyExport)
	ON_BN_CLICKED(IDC_BUTTON_NETWORK_DETAILS, &WinMTRDialog::OnNetworkDetails)
	ON_COMMAND(ID_EXPORT_COPY_TEXT, &WinMTRDialog::OnExportCopyText)
	ON_COMMAND(ID_EXPORT_COPY_HTML, &WinMTRDialog::OnExportCopyHtml)
	ON_COMMAND(ID_EXPORT_SAVE_TEXT, &WinMTRDialog::OnExportSaveText)
	ON_COMMAND(ID_EXPORT_SAVE_HTML, &WinMTRDialog::OnExportSaveHtml)
	ON_COMMAND(ID_EXPORT_SAVE_CSV, &WinMTRDialog::OnExportSaveCsv)
	ON_COMMAND(ID_EXPORT_SAVE_JSON, &WinMTRDialog::OnExportSaveJson)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_MTR, &WinMTRDialog::OnDblclkList)
	ON_CBN_SELCHANGE(IDC_COMBO_HOST, &WinMTRDialog::OnCbnSelchangeComboHost)
	ON_CBN_SELENDOK(IDC_COMBO_HOST, &WinMTRDialog::OnCbnSelendokComboHost)
	ON_CBN_CLOSEUP(IDC_COMBO_HOST, &WinMTRDialog::OnCbnCloseupComboHost)
	ON_MESSAGE(messageTraceFinished, &WinMTRDialog::OnTraceFinished)
	ON_MESSAGE(messageNetworkInfoReady, &WinMTRDialog::OnNetworkInfoReady)
	ON_MESSAGE(messageTraceDataChanged, &WinMTRDialog::OnTraceDataChanged)
	ON_MESSAGE(messageNetworkInterfaceChanged, &WinMTRDialog::OnNetworkInterfaceChanged)
END_MESSAGE_MAP()

WinMTRDialog::WinMTRDialog(CWnd* parent) noexcept
	: CDialog(IDD, parent),
	interval(WinMTRUtils::DEFAULT_INTERVAL),
	packetSize(WinMTRUtils::DEFAULT_PING_SIZE),
	maxHops(WinMTRUtils::DEFAULT_MAX_HOPS),
	timeoutMs(WinMTRUtils::DEFAULT_TIMEOUT_MS),
	cycles(WinMTRUtils::DEFAULT_CYCLES),
	tos(WinMTRUtils::DEFAULT_TOS),
	payloadPattern(WinMTRUtils::DEFAULT_PAYLOAD_PATTERN),
	startTtl(WinMTRUtils::DEFAULT_START_TTL),
	minimumTtl(WinMTRUtils::DEFAULT_MINIMUM_TTL),
	unknownHostLimit(WinMTRUtils::DEFAULT_UNKNOWN_HOST_LIMIT),
	ecmpDisplayLimit(WinMTRUtils::DEFAULT_ECMP_DISPLAY_LIMIT),
	replyCacheSeconds(WinMTRUtils::DEFAULT_REPLY_CACHE_SECONDS),
	resolveNames(WinMTRUtils::DEFAULT_USE_DNS),
	lookupAsnIsp(WinMTRUtils::DEFAULT_LOOKUP_ASN_ISP),
	dontFragment(WinMTRUtils::DEFAULT_DONT_FRAGMENT),
	useIPv4(WinMTRUtils::DEFAULT_USE_IPV4),
	useIPv6(WinMTRUtils::DEFAULT_USE_IPV6),
	queryPublicInfo(WinMTRUtils::DEFAULT_QUERY_PUBLIC_NETWORK_INFO),
	publicInfoRefreshMode(WinMTRUtils::DEFAULT_PUBLIC_INFO_REFRESH_MODE),
	publicInfoRefreshMinutes(WinMTRUtils::DEFAULT_PUBLIC_INFO_REFRESH_MINUTES),
	historyLimit(WinMTRUtils::DEFAULT_MAX_LRU)
{
	icon = AfxGetApp()->LoadIconW(IDR_MAINFRAME);
	wmtrnet = std::make_shared<WinMTRNet>(this);
}

WinMTRDialog::~WinMTRDialog()
{
	if (networkInterfaceNotification != nullptr) {
		CancelMibChangeNotify2(networkInterfaceNotification);
		networkInterfaceNotification = nullptr;
	}
	stopTrace();
	stopNetworkInfoQuery();
	if (traceThread) traceThread.reset();
	if (networkInfoThread) networkInfoThread.reset();
}

void WinMTRDialog::DoDataExchange(CDataExchange* exchange)
{
	CDialog::DoDataExchange(exchange);
	DDX_Control(exchange, IDC_BUTTON_OPTIONS, buttonOptions);
	DDX_Control(exchange, IDC_BUTTON_START_STOP, buttonStart);
	DDX_Control(exchange, IDC_BUTTON_RESET_STATS, buttonReset);
	DDX_Control(exchange, IDC_BUTTON_SCREENSHOT, buttonScreenshot);
	DDX_Control(exchange, IDC_BUTTON_COPY_EXPORT, buttonCopyExport);
	DDX_Control(exchange, IDC_BUTTON_NETWORK_DETAILS, buttonNetworkDetails);
	DDX_Control(exchange, IDC_COMBO_HOST, comboHost);
	DDX_Control(exchange, IDC_LIST_MTR, listMtr);
	DDX_Control(exchange, IDC_GROUP_TARGET, groupTarget);
	DDX_Control(exchange, IDC_GROUP_ACTIONS, groupActions);
	DDX_Control(exchange, IDC_GROUP_PUBLIC_INFO, groupPublicInfo);
}

BOOL WinMTRDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetWindowTextW(WinMTRBranding::main_window_title.data());
	SetIcon(icon, TRUE);
	SetIcon(icon, FALSE);
	SetTimer(dialogTimerId, dialogTimerMs, nullptr);

	if (!statusBar.Create(this)) {
		AfxMessageBox(IDS_ERROR_STATUS_BAR, MB_OK | MB_ICONERROR);
	}
	else {
		statusBar.GetStatusBarCtrl().SetMinHeight(scaled(GetSafeHwnd(), 25));
		statusBar.GetStatusBarCtrl().SetBkColor(GetSysColor(COLOR_3DFACE));
		UINT indicators[] = { IDS_STATUS_READY, IDS_APP_COMPANY };
		statusBar.SetIndicators(std::span<UINT>{ indicators });
		statusBar.SetPaneInfo(0, statusBar.GetItemID(0), SBPS_STRETCH, 0);
		statusBar.SetPaneInfo(1, statusBar.GetItemID(1), SBPS_NORMAL, scaled(GetSafeHwnd(), 205));
		if (companyLink.Create(WinMTRBranding::company_name.data(), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			CRect(0, 0, 0, 0), &statusBar, IDS_APP_COMPANY)) {
			companyLink.SetURL(WinMTRBranding::company_url.data());
			companyLink.m_bTransparent = TRUE;
			statusBar.AddPaneControl(&companyLink, statusBar.GetItemID(1), FALSE);
			// The link control is the pane's only visible content. Leaving the
			// indicator text in place paints a second copy underneath it.
			statusBar.SetPaneText(1, L"");
		}
	}

	configureList();
	applyTechnicalFonts(effectiveDpi(GetSafeHwnd()));
	// The themed group-box interior is white while ordinary dialog labels use
	// COLOR_3DFACE. Use the classic group-box frame only for these containers so
	// captions, values and the surrounding dialog share one background colour.
	for (const int id : { IDC_GROUP_TARGET, IDC_GROUP_ACTIONS, IDC_GROUP_PUBLIC_INFO }) {
		if (CWnd* group = GetDlgItem(id); group != nullptr) {
			SetWindowTheme(group->GetSafeHwnd(), L"", L"");
		}
	}
	InitRegistry();
	configureNetworkInterfaceNotification();
	buttonNetworkDetails.EnableWindow(FALSE);
	setStatus(loadString(IDS_STATUS_READY));

	CRect publicRect;
	groupPublicInfo.GetWindowRect(publicRect);
	ScreenToClient(publicRect);
	topContentBottom = publicRect.bottom;

	if (queryPublicInfo.load()) startNetworkInfoQuery();
	else {
		SetDlgItemTextW(IDC_STATIC_PUBLIC_IP, loadString(IDS_PUBLIC_INFO_QUERY_FAILED));
		for (const int id : { IDC_STATIC_PUBLIC_HOSTNAME, IDC_STATIC_PUBLIC_COUNTRY,
			IDC_STATIC_PUBLIC_CITY, IDC_STATIC_PUBLIC_ASN, IDC_STATIC_PUBLIC_ISP }) {
			SetDlgItemTextW(id, loadString(IDS_VALUE_UNAVAILABLE));
		}
	}
	CRect initialClient;
	GetClientRect(initialClient);
	layoutControls(initialClient.Width(), initialClient.Height());
	resizeWindowToContent();

	if (autoStart) {
		comboHost.SetWindowTextW(defaultHostname.c_str());
		PostMessageW(WM_COMMAND, MAKEWPARAM(IDC_BUTTON_START_STOP, BN_CLICKED),
			reinterpret_cast<LPARAM>(buttonStart.GetSafeHwnd()));
	}
	else {
		comboHost.SetFocus();
	}
	return FALSE;
}

void WinMTRDialog::configureList()
{
	listMtr.SetExtendedStyle(listMtr.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
		LVS_EX_DOUBLEBUFFER);
	for (int index = 0; index < static_cast<int>(columnStringIds.size()); ++index) {
		const auto title = loadString(columnStringIds[static_cast<size_t>(index)]);
		const int initialWidth = index == 0 ? 170 : (index == 13 ? 180 : 64);
		listMtr.InsertColumn(index, title, index >= 1 && index <= 10 ? LVCFMT_RIGHT : LVCFMT_LEFT,
			scaled(GetSafeHwnd(), initialWidth));
	}
	listMtr.ShowWindow(SW_HIDE);
}

void WinMTRDialog::applyTechnicalFonts(UINT dpi)
{
	createTechnicalFont(tableFont, dpi == 0 ? 96 : dpi, 85);
	createTechnicalFont(technicalFont, dpi == 0 ? 96 : dpi, 90);
	listMtr.SetFont(&tableFont);
	comboHost.SetFont(&technicalFont);
	for (const int id : { IDC_STATIC_PUBLIC_IP, IDC_STATIC_PUBLIC_HOSTNAME,
		IDC_STATIC_PUBLIC_COUNTRY, IDC_STATIC_PUBLIC_CITY,
		IDC_STATIC_PUBLIC_ASN, IDC_STATIC_PUBLIC_ISP }) {
		if (CWnd* control = GetDlgItem(id); control != nullptr) control->SetFont(&technicalFont);
	}
}

void WinMTRDialog::setStatus(const wchar_t* text)
{
	if (::IsWindow(statusBar.GetSafeHwnd())) statusBar.SetPaneText(0, text == nullptr ? L"" : text);
}

void WinMTRDialog::OnSize(UINT type, int width, int height)
{
	CDialog::OnSize(type, width, height);
	if (type != SIZE_MINIMIZED) layoutControls(width, height);
}

void WinMTRDialog::layoutControls(int clientWidth, int clientHeight)
{
	if (!::IsWindow(groupTarget.GetSafeHwnd())) return;
	// Moving every child one by one lets Windows repaint intermediate layouts,
	// which leaves group-box frames and buttons visible at their old positions.
	// Suppress those intermediate paints and render the completed layout once.
	SetRedraw(FALSE);
	const int margin = scaled(GetSafeHwnd(), 10);
	const int gap = scaled(GetSafeHwnd(), 8);
	const int groupHeight = scaled(GetSafeHwnd(), 52);
	const int controlHeight = scaled(GetSafeHwnd(), 26);
	const int actionWidth = scaled(GetSafeHwnd(), 442);
	// 800 logical client pixels fits the 330-pixel target group, the 442-pixel
	// action group, their gap and both outer margins.  The normal 830-pixel
	// window therefore remains a single row after non-client borders are removed.
	const bool stackedGroups = clientWidth < scaled(GetSafeHwnd(), 800);
	const int targetWidth = stackedGroups ? clientWidth - margin * 2
		: std::max(scaled(GetSafeHwnd(), 330), clientWidth - margin * 2 - gap - actionWidth);
	const int actionX = stackedGroups ? margin : margin + targetWidth + gap;
	const int actionY = stackedGroups ? margin + groupHeight + gap : margin;
	const int actualActionWidth = stackedGroups ? clientWidth - margin * 2
		: std::max(actionWidth, clientWidth - margin * 2 - gap - targetWidth);
	const int targetControlTop = margin + scaled(GetSafeHwnd(), 19);
	const int actionControlTop = actionY + scaled(GetSafeHwnd(), 19);

	moveControl(this, IDC_GROUP_TARGET, margin, margin, targetWidth, groupHeight);
	moveControl(this, IDC_GROUP_ACTIONS, actionX, actionY, actualActionWidth, groupHeight);
	const int labelWidth = scaled(GetSafeHwnd(), 58);
	const int startWidth = scaled(GetSafeHwnd(), 86);
	moveControl(this, IDC_STATIC_HOST_LABEL, margin + gap, targetControlTop, labelWidth, controlHeight);
	moveControl(this, IDC_BUTTON_START_STOP, margin + targetWidth - gap - startWidth, targetControlTop, startWidth, controlHeight);
	moveControl(this, IDC_COMBO_HOST, margin + gap + labelWidth, targetControlTop,
		targetWidth - gap * 3 - labelWidth - startWidth, scaled(GetSafeHwnd(), 220));

	const std::array<int, 4> actionIds{ IDC_BUTTON_OPTIONS, IDC_BUTTON_RESET_STATS,
		IDC_BUTTON_SCREENSHOT, IDC_BUTTON_COPY_EXPORT };
	const std::array<int, 4> actionWidths{ scaled(GetSafeHwnd(), 70), scaled(GetSafeHwnd(), 92),
		scaled(GetSafeHwnd(), 132), scaled(GetSafeHwnd(), 104) };
	const int totalActionWidth = std::accumulate(actionWidths.begin(), actionWidths.end(), gap * 3);
	int actionControlX = std::max(actionX + gap,
		actionX + actualActionWidth - gap - totalActionWidth);
	for (size_t index = 0; index < actionIds.size(); ++index) {
		moveControl(this, actionIds[index], actionControlX, actionControlTop, actionWidths[index], controlHeight);
		actionControlX += actionWidths[index] + gap;
	}

	const int publicTop = actionY + groupHeight + gap;
	const int publicHeight = scaled(GetSafeHwnd(), 76);
	moveControl(this, IDC_GROUP_PUBLIC_INFO, margin, publicTop, clientWidth - margin * 2, publicHeight);
	const int detailWidth = scaled(GetSafeHwnd(), 110);
	const int detailX = clientWidth - margin - gap - detailWidth;
	moveControl(this, IDC_BUTTON_NETWORK_DETAILS, detailX,
		publicTop + (publicHeight - controlHeight) / 2 + scaled(GetSafeHwnd(), 3), detailWidth, controlHeight);

	const int available = detailX - (margin + gap * 2);
	const int line1 = publicTop + scaled(GetSafeHwnd(), 22);
	const int line2 = publicTop + scaled(GetSafeHwnd(), 47);
	const int textHeight = scaled(GetSafeHwnd(), 18);
	const int firstX = margin + gap;

	// Public information uses four fields on the first row and two on the
	// second. Natural text widths are preserved unless the screen leaves no room.
	const std::array<int, 4> row1LabelIds{
		IDC_STATIC_PUBLIC_IP_LABEL, IDC_STATIC_PUBLIC_COUNTRY_LABEL,
		IDC_STATIC_PUBLIC_CITY_LABEL, IDC_STATIC_PUBLIC_ASN_LABEL
	};
	const std::array<int, 4> row1Labels{
		publicLabelWidth(this, row1LabelIds[0]), publicLabelWidth(this, row1LabelIds[1]),
		publicLabelWidth(this, row1LabelIds[2]), publicLabelWidth(this, row1LabelIds[3])
	};
	const std::array<int, 4> row1ValueIds{
		IDC_STATIC_PUBLIC_IP, IDC_STATIC_PUBLIC_COUNTRY,
		IDC_STATIC_PUBLIC_CITY, IDC_STATIC_PUBLIC_ASN
	};
	const std::array<int, 4> row1MinimumValues{
		scaled(GetSafeHwnd(), 40), scaled(GetSafeHwnd(), 20),
		scaled(GetSafeHwnd(), 30), scaled(GetSafeHwnd(), 30)
	};
	std::array<int, 4> row1Widths{};
	std::array<int, 4> row1MinimumWidths{};
	for (size_t index = 0; index < row1Widths.size(); ++index) {
		row1MinimumWidths[index] = row1Labels[index] + row1MinimumValues[index];
		row1Widths[index] = row1Labels[index] + std::max(row1MinimumValues[index],
			controlTextWidth(this, row1ValueIds[index]) + scaled(GetSafeHwnd(), 8));
	}
	const auto fitWidths = [](auto& widths, const auto& minimumWidths, int budget) {
		const int desiredTotal = std::accumulate(widths.begin(), widths.end(), 0);
		const int minimumTotal = std::accumulate(minimumWidths.begin(), minimumWidths.end(), 0);
		if (desiredTotal <= budget || budget <= minimumTotal) return;
		const int extraBudget = budget - minimumTotal;
		const int extraWanted = desiredTotal - minimumTotal;
		int assignedExtra = 0;
		for (size_t index = 0; index < widths.size(); ++index) {
			const int wanted = widths[index] - minimumWidths[index];
			const int extra = index + 1 == widths.size()
				? extraBudget - assignedExtra
				: MulDiv(extraBudget, wanted, extraWanted);
			widths[index] = minimumWidths[index] + extra;
			assignedExtra += extra;
		}
	};

	const int hostnameLabelWidth = publicLabelWidth(this, IDC_STATIC_PUBLIC_HOSTNAME_LABEL);
	const int ispLabelWidth = publicLabelWidth(this, IDC_STATIC_PUBLIC_ISP_LABEL);
	std::array<int, 2> row2MinimumWidths{
		hostnameLabelWidth + scaled(GetSafeHwnd(), 40),
		ispLabelWidth + scaled(GetSafeHwnd(), 60)
	};
	std::array<int, 2> row2Widths{
		hostnameLabelWidth + std::max(scaled(GetSafeHwnd(), 40),
			controlTextWidth(this, IDC_STATIC_PUBLIC_HOSTNAME) + scaled(GetSafeHwnd(), 8)),
		ispLabelWidth + std::max(scaled(GetSafeHwnd(), 60),
			controlTextWidth(this, IDC_STATIC_PUBLIC_ISP) + scaled(GetSafeHwnd(), 8))
	};
	fitWidths(row2Widths, row2MinimumWidths, std::max(2, available - gap));
	const int row2Hostname = row2Widths[0];
	const int row2Isp = row2Widths[1];
	const int row2Total = row2Hostname + gap + row2Isp;

	fitWidths(row1Widths, row1MinimumWidths, std::max(4, available - gap * 3));
	const int row1ContentWidth = std::accumulate(row1Widths.begin(), row1Widths.end(), 0);
	const int row1GapTotal = std::min(std::max(0, available - row1ContentWidth),
		std::max(gap * 3, row2Total - row1ContentWidth));
	std::array<int, 3> row1Gaps{
		row1GapTotal / 3, row1GapTotal / 3, row1GapTotal / 3
	};
	for (int remainder = row1GapTotal % 3, index = 0; remainder > 0; --remainder, ++index) {
		++row1Gaps[static_cast<size_t>(index)];
	}
	const int row1Ip = row1Widths[0];
	const int row1Country = row1Widths[1];
	const int row1City = row1Widths[2];
	const int row1Asn = row1Widths[3];
	const int row1CountryX = firstX + row1Ip + row1Gaps[0];
	const int row1CityX = row1CountryX + row1Country + row1Gaps[1];
	const int row1AsnX = row1CityX + row1City + row1Gaps[2];
	const int row2IspX = firstX + row2Hostname + gap;

	moveControl(this, IDC_STATIC_PUBLIC_IP_LABEL, firstX, line1,
		row1Labels[0] - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_IP, firstX + row1Labels[0], line1,
		row1Ip - row1Labels[0], textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_COUNTRY_LABEL, row1CountryX, line1,
		row1Labels[1] - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_COUNTRY, row1CountryX + row1Labels[1], line1,
		row1Country - row1Labels[1], textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_CITY_LABEL, row1CityX, line1,
		row1Labels[2] - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_CITY, row1CityX + row1Labels[2], line1,
		row1City - row1Labels[2], textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_ASN_LABEL, row1AsnX, line1,
		row1Labels[3] - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_ASN, row1AsnX + row1Labels[3], line1,
		row1Asn - row1Labels[3], textHeight);

	moveControl(this, IDC_STATIC_PUBLIC_HOSTNAME_LABEL, firstX, line2,
		hostnameLabelWidth - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_HOSTNAME, firstX + hostnameLabelWidth, line2,
		row2Hostname - hostnameLabelWidth, textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_ISP_LABEL, row2IspX, line2,
		ispLabelWidth - scaled(GetSafeHwnd(), 2), textHeight);
	moveControl(this, IDC_STATIC_PUBLIC_ISP, row2IspX + ispLabelWidth, line2,
		row2Isp - ispLabelWidth, textHeight);
	topContentBottom = publicTop + publicHeight;

	if (listIsVisible) {
		int statusHeight = scaled(GetSafeHwnd(), 25);
		if (::IsWindow(statusBar.GetSafeHwnd())) {
			CRect statusRect;
			statusBar.GetWindowRect(statusRect);
			statusHeight = statusRect.Height();
		}
		const int listTop = topContentBottom + gap;
		moveControl(this, IDC_LIST_MTR, margin, listTop, clientWidth - margin * 2,
			clientHeight - statusHeight - listTop - gap);
	}
	if (::IsWindow(statusBar.GetSafeHwnd())) RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);
	SetRedraw(TRUE);
	if (!comboHost.IsWindowEnabled()) {
		comboHost.SetEditSel(0, 0);
		COMBOBOXINFO info{ .cbSize = sizeof(COMBOBOXINFO) };
		if (GetComboBoxInfo(comboHost.GetSafeHwnd(), &info) && info.hwndItem != nullptr) {
			::SendMessageW(info.hwndItem, EM_SETSEL, 0, 0);
		}
	}
	RedrawWindow(nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

CSize WinMTRDialog::minimumWindowSize()
{
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
	const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
	CRect windowRect;
	CRect clientRect;
	GetWindowRect(windowRect);
	GetClientRect(clientRect);
	const int frameWidth = windowRect.Width() - clientRect.Width();
	const int frameHeight = windowRect.Height() - clientRect.Height();
	const int margin = scaled(GetSafeHwnd(), 10);
	const int gap = scaled(GetSafeHwnd(), 8);
	const int detailWidth = scaled(GetSafeHwnd(), 110);
	const int publicClientWidth = naturalPublicInformationWidth(this)
		+ margin * 2 + gap * 3 + detailWidth;
	int desiredWidth = std::max(scaled(GetSafeHwnd(), 830),
		std::max(scaled(GetSafeHwnd(), 800), publicClientWidth) + frameWidth);
	int statusHeight = scaled(GetSafeHwnd(), 25);
	if (::IsWindow(statusBar.GetSafeHwnd())) {
		CRect statusRect;
		statusBar.GetWindowRect(statusRect);
		statusHeight = statusRect.Height();
	}
	int desiredHeight = std::max(scaled(GetSafeHwnd(), 220),
		topContentBottom + gap + statusHeight + frameHeight);

	if (listIsVisible && !displayRows.empty()) {
		const int columnsWidth = naturalColumnsWidth > 0
			? naturalColumnsWidth
			: scaled(GetSafeHwnd(), 830);
		const int listFrameWidth = GetSystemMetrics(SM_CXEDGE) * 2;
		int listFrameHeight = GetSystemMetrics(SM_CYEDGE) * 2 + scaled(GetSafeHwnd(), 2);
		CRect itemRect;
		int rowHeight = scaled(GetSafeHwnd(), 22);
		if (listMtr.GetItemRect(0, itemRect, LVIR_BOUNDS)) rowHeight = itemRect.Height();
		int headerHeight = scaled(GetSafeHwnd(), 24);
		if (HWND header = ListView_GetHeader(listMtr.GetSafeHwnd()); header != nullptr) {
			RECT headerRect{};
			if (::GetWindowRect(header, &headerRect)) headerHeight = headerRect.bottom - headerRect.top;
		}
		const int requiredListWidth = columnsWidth + listFrameWidth;
		const int maximumListWidth = std::max(1, workWidth - frameWidth - margin * 2);
		if (requiredListWidth > maximumListWidth) {
			listFrameHeight += GetSystemMetrics(SM_CYHSCROLL);
		}
		desiredWidth = std::max(desiredWidth,
			requiredListWidth + margin * 2 + frameWidth);
		desiredHeight = std::max(desiredHeight,
			topContentBottom + scaled(GetSafeHwnd(), 8) + headerHeight
			+ rowHeight * static_cast<int>(displayRows.size()) + listFrameHeight
			+ statusHeight + gap + frameHeight);
	}
	return CSize(std::min(workWidth, desiredWidth), std::min(workHeight, desiredHeight));
}

void WinMTRDialog::resizeWindowToContent()
{
	const CSize desired = minimumWindowSize();
	CRect current;
	GetWindowRect(current);
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int desiredWidth = static_cast<int>(desired.cx);
	const int desiredHeight = static_cast<int>(desired.cy);
	const int left = std::clamp(static_cast<int>(current.left), static_cast<int>(monitor.rcWork.left),
		static_cast<int>(monitor.rcWork.right) - desiredWidth);
	const int top = std::clamp(static_cast<int>(current.top), static_cast<int>(monitor.rcWork.top),
		static_cast<int>(monitor.rcWork.bottom) - desiredHeight);
	if (current.left != left || current.top != top
		|| current.Width() != desiredWidth || current.Height() != desiredHeight) {
		SetWindowPos(nullptr, left, top, desiredWidth, desiredHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void WinMTRDialog::OnSizing(UINT side, LPRECT rect)
{
	CDialog::OnSizing(side, rect);
	const CSize minimum = minimumWindowSize();
	if (rect->right - rect->left < minimum.cx) {
		if (side == WMSZ_LEFT || side == WMSZ_TOPLEFT || side == WMSZ_BOTTOMLEFT) rect->left = rect->right - minimum.cx;
		else rect->right = rect->left + minimum.cx;
	}
	if (rect->bottom - rect->top < minimum.cy) {
		if (side == WMSZ_TOP || side == WMSZ_TOPLEFT || side == WMSZ_TOPRIGHT) rect->top = rect->bottom - minimum.cy;
		else rect->bottom = rect->top + minimum.cy;
	}
}

void WinMTRDialog::OnGetMinMaxInfo(MINMAXINFO* info)
{
	CDialog::OnGetMinMaxInfo(info);
	const CSize minimum = minimumWindowSize();
	info->ptMinTrackSize.x = minimum.cx;
	info->ptMinTrackSize.y = minimum.cy;
}

BOOL WinMTRDialog::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result)
{
	const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
	const HWND header = ::IsWindow(listMtr.GetSafeHwnd()) ? ListView_GetHeader(listMtr.GetSafeHwnd()) : nullptr;
	if (notification != nullptr && notification->hwndFrom == header
		&& (notification->code == HDN_BEGINTRACKA || notification->code == HDN_BEGINTRACKW)) {
		if (result != nullptr) *result = TRUE;
		return TRUE;
	}
	return CDialog::OnNotify(wParam, lParam, result);
}

LRESULT WinMTRDialog::OnDpiChanged(WPARAM newDpi, LPARAM suggestedRect)
{
	const auto* suggested = reinterpret_cast<const RECT*>(suggestedRect);
	if (suggested != nullptr) {
		SetWindowPos(nullptr, suggested->left, suggested->top,
			suggested->right - suggested->left, suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	const UINT dpi = LOWORD(newDpi);
	applyTechnicalFonts(dpi);
	if (::IsWindow(statusBar.GetSafeHwnd())) {
		statusBar.GetStatusBarCtrl().SetMinHeight(MulDiv(25, static_cast<int>(dpi), 96));
		statusBar.SetPaneInfo(1, statusBar.GetItemID(1), SBPS_NORMAL,
			MulDiv(205, static_cast<int>(dpi), 96));
	}
	CRect client;
	GetClientRect(client);
	layoutControls(client.Width(), client.Height());
	if (listIsVisible) {
		// The data revision does not change when the window crosses monitors,
		// but the new font metrics do. Force a full row/column measurement so
		// every column remains wide enough at the new DPI.
		lastRenderedRevision = ~std::uint64_t{};
		DisplayRedraw();
	}
	return 0;
}

void WinMTRDialog::OnPaint()
{
	if (!IsIconic()) {
		CDialog::OnPaint();
		return;
	}
	CPaintDC dc(this);
	SendMessageW(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
	const int width = GetSystemMetrics(SM_CXICON);
	const int height = GetSystemMetrics(SM_CYICON);
	CRect rect;
	GetClientRect(rect);
	dc.DrawIcon((rect.Width() - width) / 2, (rect.Height() - height) / 2, icon);
}

BOOL WinMTRDialog::OnEraseBkgnd(CDC* dc)
{
	if (dc == nullptr) return CDialog::OnEraseBkgnd(dc);
	CRect client;
	GetClientRect(client);
	dc->FillSolidRect(client, GetSysColor(COLOR_3DFACE));
	return TRUE;
}

HBRUSH WinMTRDialog::OnCtlColor(CDC* dc, CWnd* window, UINT controlType)
{
	HBRUSH brush = CDialog::OnCtlColor(dc, window, controlType);
	const int id = window == nullptr ? 0 : window->GetDlgCtrlID();
	const bool isGroupBox = id == IDC_GROUP_TARGET || id == IDC_GROUP_ACTIONS
		|| id == IDC_GROUP_PUBLIC_INFO;
	const bool isPublicValue = id == IDC_STATIC_PUBLIC_IP || id == IDC_STATIC_PUBLIC_HOSTNAME
		|| id == IDC_STATIC_PUBLIC_COUNTRY || id == IDC_STATIC_PUBLIC_CITY
		|| id == IDC_STATIC_PUBLIC_ASN || id == IDC_STATIC_PUBLIC_ISP;
	if (controlType == CTLCOLOR_STATIC || (controlType == CTLCOLOR_BTN && isGroupBox)) {
		const COLORREF background = GetSysColor(COLOR_3DFACE);
		dc->SetBkMode(OPAQUE);
		dc->SetBkColor(background);
		if (isPublicValue) dc->SetTextColor(RGB(32, 72, 112));
		brush = GetSysColorBrush(COLOR_3DFACE);
	}
	return brush;
}

HCURSOR WinMTRDialog::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(icon);
}

void WinMTRDialog::SetHostName(std::wstring host)
{
	autoStart = true;
	defaultHostname = std::move(host);
}

void WinMTRDialog::SetPingSize(unsigned value, options_source source) noexcept
{
	packetSize = value;
	hasPacketSizeFromCommandLine = source == options_source::cmd_line;
}

void WinMTRDialog::SetMaxLRU(int value, options_source source) noexcept
{
	historyLimit = static_cast<unsigned>(std::max(value, 1));
	hasHistoryLimitFromCommandLine = source == options_source::cmd_line;
}

void WinMTRDialog::SetInterval(float value, options_source source) noexcept
{
	interval = value;
	hasIntervalFromCommandLine = source == options_source::cmd_line;
}

void WinMTRDialog::SetUseDNS(bool value, options_source source) noexcept
{
	resolveNames = value;
	hasResolveNamesFromCommandLine = source == options_source::cmd_line;
}

void WinMTRDialog::OnCancel()
{
	OnClose();
}

int WinMTRDialog::DisplayRedraw()
{
	const auto snapshot = wmtrnet->getTraceSnapshot();
	const bool hasData = std::any_of(snapshot.hops.begin(), snapshot.hops.end(), [](const s_nethost& hop) {
		return hop.xmit != 0 || !hop.responders.empty();
	});
	if (!hasData) {
		if (snapshot.revision != lastRenderedRevision) {
			lastRenderedRevision = snapshot.revision;
			listMtr.DeleteAllItems();
			displayRows.clear();
			listMtr.ShowWindow(SW_HIDE);
			listIsVisible = false;
			lastAutoRowCount = 0;
			CRect client;
			GetClientRect(client);
			layoutControls(client.Width(), client.Height());
		}
		return 0;
	}
	if (snapshot.revision == lastRenderedRevision) return 0;
	lastRenderedRevision = snapshot.revision;
	if (state == STATES::TRACING) setStatus(loadString(IDS_STATUS_TRACING));

	std::optional<DisplayRow> selectedRow;
	if (POSITION selectedPosition = listMtr.GetFirstSelectedItemPosition(); selectedPosition != nullptr) {
		const int selectedIndex = listMtr.GetNextSelectedItem(selectedPosition);
		if (selectedIndex >= 0 && selectedIndex < static_cast<int>(displayRows.size())) {
			selectedRow = displayRows[static_cast<size_t>(selectedIndex)];
		}
	}
	listMtr.SetRedraw(FALSE);
	listMtr.DeleteAllItems();
	displayRows.clear();
	const auto empty = L"";
	const auto setCell = [&](int row, int column, const std::wstring& value) {
		listMtr.SetItemText(row, column, value.c_str());
	};
	const auto setStatistics = [&](int row, const s_nethost& hop) {
		setCell(row, 2, std::format(L"{:.0f}%", hop.getLossPercent()));
		setCell(row, 3, std::to_wstring(hop.xmit));
		setCell(row, 4, std::to_wstring(hop.returned));
		setCell(row, 5, hop.returned == 0 ? empty : std::format(L"{}", hop.best));
		setCell(row, 6, hop.returned == 0 ? empty : std::format(L"{:.1f}", hop.getAverageMs()));
		setCell(row, 7, hop.returned == 0 ? empty : std::format(L"{}", hop.worst));
		setCell(row, 8, hop.returned == 0 ? empty : std::format(L"{}", hop.last));
		setCell(row, 9, hop.returned == 0 ? empty : std::format(L"{:.1f}", hop.jitter));
		setCell(row, 10, hop.returned == 0 ? empty : std::format(L"{:.1f}", hop.stddev));
	};

	for (size_t index = 0; index < snapshot.hops.size();) {
		const auto& hop = snapshot.hops[index];
		if (hop.returned == 0 && hop.responders.empty()) {
			size_t end = index;
			while (end + 1 < snapshot.hops.size() && snapshot.hops[end + 1].returned == 0 &&
				snapshot.hops[end + 1].responders.empty()) ++end;
			const unsigned firstHop = hop.hop;
			const unsigned lastHop = snapshot.hops[end].hop;
			const CString noResponse = loadString(IDS_STRING_NO_RESPONSE_FROM_HOST);
			const int row = listMtr.InsertItem(listMtr.GetItemCount(), noResponse.GetString());
			setCell(row, 1, firstHop == lastHop ? std::to_wstring(firstHop)
				: std::format(L"{}-{}", firstHop, lastHop));
			displayRows.push_back({ DisplayRowKind::unknown_range, firstHop, lastHop, 0 });
			index = end + 1;
			continue;
		}

		std::wstring primaryName = hop.getName();
		if (primaryName.empty() && !hop.responders.empty()) primaryName = hop.responders.front().getName();
		if (primaryName.empty()) primaryName = loadString(IDS_STRING_NO_RESPONSE_FROM_HOST).GetString();
		const int row = listMtr.InsertItem(listMtr.GetItemCount(), primaryName.c_str());
		setCell(row, 1, std::to_wstring(hop.hop));
		setStatistics(row, hop);
		setCell(row, 11, hop.country);
		setCell(row, 12, hop.asn);
		setCell(row, 13, hop.isp);
		displayRows.push_back({ DisplayRowKind::primary, hop.hop, hop.hop, 0 });

		const size_t visibleResponders = std::min<size_t>(hop.responders.size(), ecmpDisplayLimit.load());
		for (size_t responderIndex = 1; responderIndex < visibleResponders; ++responderIndex) {
			const auto& responder = hop.responders[responderIndex];
			const int responderRow = listMtr.InsertItem(listMtr.GetItemCount(),
				(L"  + " + responder.getName()).c_str());
			setCell(responderRow, 1, std::to_wstring(hop.hop));
			setCell(responderRow, 11, responder.country);
			setCell(responderRow, 12, responder.asn);
			setCell(responderRow, 13, responder.isp);
			displayRows.push_back({ DisplayRowKind::responder, hop.hop, hop.hop,
				responderIndex, addr_to_string(responder.addr) });
		}
		++index;
	}

	if (!listIsVisible && !displayRows.empty()) {
		listIsVisible = true;
		listMtr.ShowWindow(SW_SHOW);
	}
	naturalColumnsWidth = scaled(GetSafeHwnd(), 8);
	for (int column = 0; column < 14; ++column) {
		listMtr.SetColumnWidth(column, LVSCW_AUTOSIZE);
		const int dataWidth = listMtr.GetColumnWidth(column);
		const auto title = loadString(columnStringIds[static_cast<size_t>(column)]);
		const int headerWidth = listMtr.GetStringWidth(title) + scaled(GetSafeHwnd(), 24);
		const int measuredWidth = std::max(dataWidth, headerWidth) + scaled(GetSafeHwnd(), 6);
		listMtr.SetColumnWidth(column, measuredWidth);
		naturalColumnsWidth += measuredWidth;
	}
	if (selectedRow) {
		const auto selected = std::find_if(displayRows.begin(), displayRows.end(), [&](const DisplayRow& row) {
			return row.kind == selectedRow->kind && row.firstHop == selectedRow->firstHop
				&& row.lastHop == selectedRow->lastHop
				&& row.responderAddress == selectedRow->responderAddress;
		});
		if (selected != displayRows.end()) {
			const int index = static_cast<int>(std::distance(displayRows.begin(), selected));
			listMtr.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	listMtr.SetRedraw(TRUE);
	listMtr.Invalidate(FALSE);
	adjustWindowForRows();
	firstDataResize = false;
	return 0;
}

void WinMTRDialog::adjustWindowForRows()
{
	if (!listIsVisible || displayRows.empty()) return;
	const int columnsWidth = naturalColumnsWidth > 0
		? naturalColumnsWidth
		: scaled(GetSafeHwnd(), 830);
	const int margin = scaled(GetSafeHwnd(), 10);
	const CSize required = minimumWindowSize();
	CRect currentWindow;
	GetWindowRect(currentWindow);
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int maximumWidth = monitor.rcWork.right - monitor.rcWork.left;
	const int maximumHeight = monitor.rcWork.bottom - monitor.rcWork.top;
	const bool routeShrank = lastAutoRowCount != 0 && displayRows.size() < lastAutoRowCount;
	const bool resizeExactly = firstDataResize || routeShrank;
	const int requiredWidth = static_cast<int>(required.cx);
	const int requiredHeight = static_cast<int>(required.cy);
	const int desiredWidth = std::min(maximumWidth,
		resizeExactly ? requiredWidth : std::max(static_cast<int>(currentWindow.Width()), requiredWidth));
	const int desiredHeight = std::min(maximumHeight,
		resizeExactly ? requiredHeight : std::max(static_cast<int>(currentWindow.Height()), requiredHeight));
	const int desiredLeft = std::clamp(static_cast<int>(currentWindow.left), static_cast<int>(monitor.rcWork.left),
		static_cast<int>(monitor.rcWork.right) - desiredWidth);
	const int desiredTop = std::clamp(static_cast<int>(currentWindow.top), static_cast<int>(monitor.rcWork.top),
		static_cast<int>(monitor.rcWork.bottom) - desiredHeight);
	if (currentWindow.left != desiredLeft || currentWindow.top != desiredTop
		|| currentWindow.Width() != desiredWidth || currentWindow.Height() != desiredHeight) {
		SetWindowPos(nullptr, desiredLeft, desiredTop, desiredWidth, desiredHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	lastAutoRowCount = displayRows.size();
	CRect client;
	GetClientRect(client);
	const int remaining = client.Width() - margin * 2 - columnsWidth;
	if (remaining > 0) listMtr.SetColumnWidth(13, listMtr.GetColumnWidth(13) + remaining);
}

void WinMTRDialog::showNodeDetails(const DisplayRow& row)
{
	const auto snapshot = wmtrnet->getTraceSnapshot();
	WinMTRProperties details(this);
	if (row.kind == DisplayRowKind::unknown_range) {
		details.comment = row.firstHop == row.lastHop
			? formatString(IDS_NOTE_NO_RESPONSE_HOP, row.firstHop)
			: formatString(IDS_NOTE_NO_RESPONSE_RANGE, row.firstHop, row.lastHop);
		details.host = loadString(IDS_STRING_NO_RESPONSE_FROM_HOST).GetString();
		std::uint64_t sent = 0;
		for (const auto& hop : snapshot.hops) {
			if (hop.hop >= row.firstHop && hop.hop <= row.lastHop) sent += hop.xmit;
		}
		details.pck_sent = static_cast<int>(std::min<std::uint64_t>(sent, INT_MAX));
		details.pck_loss = sent == 0 ? 0 : 100;
		details.DoModal();
		return;
	}

	const auto found = std::find_if(snapshot.hops.begin(), snapshot.hops.end(), [&](const s_nethost& hop) {
		return hop.hop == row.firstHop;
	});
	if (found == snapshot.hops.end()) return;
	if (row.kind == DisplayRowKind::responder) {
		const auto responder = std::find_if(found->responders.begin(), found->responders.end(),
			[&](const s_netresponder& value) {
				return addr_to_string(value.addr) == row.responderAddress;
			});
		if (responder == found->responders.end()) return;
		details.host = responder->getName();
		details.ip = addr_to_string(responder->addr);
		details.country = responder->country;
		details.asn = responder->asn;
		details.isp = responder->isp;
		details.comment = formatString(IDS_NOTE_ALTERNATE_PATH, found->hop);
	}
	else {
		details.host = found->getName();
		details.ip = addr_to_string(found->addr);
		details.country = found->country;
		details.asn = found->asn;
		details.isp = found->isp;
		details.comment = formatString(IDS_NOTE_RESPONSIVE_NODE, found->hop);
	}
	details.pck_sent = static_cast<int>(std::min<std::uint64_t>(found->xmit, INT_MAX));
	details.pck_recv = static_cast<int>(std::min<std::uint64_t>(found->returned, INT_MAX));
	details.pck_loss = found->getPercent();
	details.ping_last = static_cast<float>(found->last);
	details.ping_best = static_cast<float>(found->best);
	details.ping_avrg = static_cast<float>(found->getAverageMs());
	details.ping_worst = static_cast<float>(found->worst);
	details.DoModal();
}

void WinMTRDialog::startNetworkInfoQuery()
{
	stopNetworkInfoQuery();
	buttonNetworkDetails.EnableWindow(FALSE);
	SetDlgItemTextW(IDC_STATIC_PUBLIC_IP, loadString(IDS_PUBLIC_INFO_QUERYING));
	for (const int id : { IDC_STATIC_PUBLIC_HOSTNAME, IDC_STATIC_PUBLIC_COUNTRY, IDC_STATIC_PUBLIC_CITY,
		IDC_STATIC_PUBLIC_ASN, IDC_STATIC_PUBLIC_ISP }) SetDlgItemTextW(id, loadString(IDS_VALUE_UNAVAILABLE));

	// Replacing a live jthread would join it on the UI thread.  Let the old
	// request finish cooperatively, then start the requested refresh from its
	// completion message instead.
	if (networkInfoThread && networkInfoRunning.load()) {
		networkInfoRestartPending = true;
		return;
	}
	if (networkInfoThread) networkInfoThread.reset();
	networkInfoRestartPending = false;
	lastNetworkInfoQueryTick = GetTickCount64();
	const auto generation = ++networkInfoGeneration;
	networkInfoRunning = true;
	networkInfoThread.emplace([this, generation](std::stop_token token) {
		auto value = winmtr::network_data::queryCurrent(token);
		const bool deliver = !token.stop_requested() && generation == networkInfoGeneration.load();
		if (deliver) {
			std::scoped_lock lock(networkInfoMutex);
			networkInfo = std::move(value);
		}
		networkInfoRunning = false;
		PostMessageW(messageNetworkInfoReady, static_cast<WPARAM>(generation), deliver ? 1 : 0);
	});
}

void WinMTRDialog::configureNetworkInterfaceNotification() noexcept
{
	if (networkInterfaceNotification != nullptr) return;
	HANDLE notification = nullptr;
	if (NotifyIpInterfaceChange(AF_UNSPEC, &WinMTRDialog::NetworkInterfaceChangeCallback,
		this, FALSE, &notification) == NO_ERROR) {
		networkInterfaceNotification = notification;
	}
}

VOID CALLBACK WinMTRDialog::NetworkInterfaceChangeCallback(PVOID context,
	PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) noexcept
{
	const auto dialog = static_cast<WinMTRDialog*>(context);
	if (dialog == nullptr) return;
	if (const HWND window = dialog->GetSafeHwnd(); window != nullptr) {
		::PostMessageW(window, messageNetworkInterfaceChanged, 0, 0);
	}
}

void WinMTRDialog::stopNetworkInfoQuery() noexcept
{
	++networkInfoGeneration;
	if (networkInfoThread) networkInfoThread->request_stop();
}

LRESULT WinMTRDialog::OnNetworkInfoReady(WPARAM generation, LPARAM delivered)
{
	if (networkInfoRestartPending.exchange(false)) {
		if (queryPublicInfo.load()) startNetworkInfoQuery();
		return 0;
	}
	if (generation != networkInfoGeneration.load() || delivered == 0) return 0;
	updateNetworkInfoSummary();
	return 0;
}

void WinMTRDialog::updateNetworkInfoSummary()
{
	winmtr::network_data::CurrentNetworkInfo value;
	{
		std::scoped_lock lock(networkInfoMutex);
		value = networkInfo;
	}
	const auto& address = value.ipv4.available() ? value.ipv4 : value.ipv6;
	const CString unavailable = loadString(IDS_VALUE_UNAVAILABLE);
	const CString queryFailed = loadString(IDS_PUBLIC_INFO_QUERY_FAILED);
	SetDlgItemTextW(IDC_STATIC_PUBLIC_IP, address.available() ? address.address.c_str() : queryFailed.GetString());
	SetDlgItemTextW(IDC_STATIC_PUBLIC_HOSTNAME,
		address.hostname.empty() ? unavailable.GetString() : address.hostname.c_str());
	SetDlgItemTextW(IDC_STATIC_PUBLIC_COUNTRY,
		address.country.empty() ? unavailable.GetString() : address.country.c_str());
	SetDlgItemTextW(IDC_STATIC_PUBLIC_CITY,
		address.city.empty() ? unavailable.GetString() : address.city.c_str());
	SetDlgItemTextW(IDC_STATIC_PUBLIC_ASN,
		address.asn.empty() ? unavailable.GetString() : address.asn.c_str());
	SetDlgItemTextW(IDC_STATIC_PUBLIC_ISP,
		address.isp.empty() ? unavailable.GetString() : address.isp.c_str());
	buttonNetworkDetails.EnableWindow(value.complete && value.anyAvailable());
	// Re-size all public-information fields after the real values arrive.
	CRect client;
	GetClientRect(client);
	layoutControls(client.Width(), client.Height());
	resizeWindowToContent();
}

void WinMTRDialog::showNetworkInfoDialog()
{
	winmtr::network_data::CurrentNetworkInfo value;
	{
		std::scoped_lock lock(networkInfoMutex);
		value = networkInfo;
	}
	if (!value.complete || !value.anyAvailable()) return;
	NetworkInfoDialog dialog(winmtr::network_data::formatDetails(value), this);
	dialog.DoModal();
}

void WinMTRDialog::OnNetworkDetails()
{
	showNetworkInfoDialog();
}

void WinMTRDialog::OnDblclkList(NMHDR* header, LRESULT* result)
{
	*result = 0;
	const auto* activation = reinterpret_cast<const NMITEMACTIVATE*>(header);
	const int item = activation == nullptr ? -1 : activation->iItem;
	if (item >= 0 && item < static_cast<int>(displayRows.size())) showNodeDetails(displayRows[static_cast<size_t>(item)]);
}

void WinMTRDialog::OnCbnSelchangeComboHost() {}
void WinMTRDialog::OnCbnSelendokComboHost() {}

void WinMTRDialog::OnCbnCloseupComboHost()
{
	if (comboHost.GetCount() > 0 && comboHost.GetCurSel() == comboHost.GetCount() - 1) ClearHistory();
}

void WinMTRDialog::OnResetStatistics()
{
	wmtrnet->ResetHops();
	listMtr.DeleteAllItems();
	displayRows.clear();
	lastAutoRowCount = 0;
	listMtr.ShowWindow(SW_HIDE);
	listIsVisible = false;
	firstDataResize = true;
	lastRenderedRevision = ~std::uint64_t{};
	setStatus(loadString(IDS_STATUS_STATS_RESET));
	CRect rect;
	GetClientRect(rect);
	layoutControls(rect.Width(), rect.Height());
}

void WinMTRDialog::OnCopyExport() { showCopyExportMenu(); }
void WinMTRDialog::OnScreenshot() { screenshotToClipboard(); }
void WinMTRDialog::OnExportCopyText() { copyText(); }
void WinMTRDialog::OnExportCopyHtml() { copyHtml(); }
void WinMTRDialog::OnExportSaveText() { exportText(); }
void WinMTRDialog::OnExportSaveHtml() { exportHtml(); }
void WinMTRDialog::OnExportSaveCsv() { exportCsv(); }
void WinMTRDialog::OnExportSaveJson() { exportJson(); }
