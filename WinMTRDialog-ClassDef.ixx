module;
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcmn.h>
#include <afxlinkctrl.h>
#include <iphlpapi.h>
#include "resource.h"
#include "WinMTRBranding.h"
#include "WinMTRNetworkData.h"

export module WinMTR.Dialog:ClassDef;

import <array>;
import <atomic>;
import <cstddef>;
import <cstdint>;
import <memory>;
import <mutex>;
import <optional>;
import <stop_token>;
import <string>;
import <thread>;
import <vector>;
import WinMTROptionsProvider;
import WinMTRStatusBar;
import WinMTR.Net;
import WinMTRUtils;

export class WinMTRDialog final : public CDialog, public IWinMTROptionsProvider
{
public:
	explicit WinMTRDialog(CWnd* parent = nullptr) noexcept;
	~WinMTRDialog() override;

	enum { IDD = IDD_WINMTR_DIALOG };
	enum class options_source : bool { none, cmd_line };
	enum class STATES { IDLE = 0, TRACING, STOPPING, EXIT };

	WinMTRStatusBar statusBar;

	BOOL InitRegistry() noexcept;
	bool InitMTRNet() noexcept;
	int DisplayRedraw();
	void Transit(STATES newState);

	void SetHostName(std::wstring host);
	void SetInterval(float value, options_source source = options_source::none) noexcept;
	void SetPingSize(unsigned value, options_source source = options_source::none) noexcept;
	void SetMaxLRU(int value, options_source source = options_source::none) noexcept;
	void SetUseDNS(bool value, options_source source = options_source::none) noexcept;
	void SetMaxHops(unsigned value, options_source source = options_source::none) noexcept;
	void SetTimeoutMs(unsigned value, options_source source = options_source::none) noexcept;
	void SetCycles(unsigned value, options_source source = options_source::none) noexcept;
	void SetTos(unsigned value, options_source source = options_source::none) noexcept;
	void SetPayloadPattern(int value, options_source source = options_source::none) noexcept;
	void SetStartTtl(unsigned value, options_source source = options_source::none) noexcept;
	void SetMinimumTtl(unsigned value, options_source source = options_source::none) noexcept;
	void SetUnknownHostLimit(unsigned value, options_source source = options_source::none) noexcept;
	void SetEcmpDisplayLimit(unsigned value, options_source source = options_source::none) noexcept;
	void SetReplyCacheSeconds(unsigned value, options_source source = options_source::none) noexcept;
	void SetLookupAsnIsp(bool value, options_source source = options_source::none) noexcept;
	void SetDontFragment(bool value, options_source source = options_source::none) noexcept;
	void SetAddressFamilies(bool ipv4, bool ipv6,
		options_source source = options_source::none) noexcept;
	void SetQueryPublicInfo(bool value, options_source source = options_source::none) noexcept;
	void SetPublicInfoRefresh(unsigned mode, unsigned minutes,
		options_source source = options_source::none) noexcept;

	[[nodiscard]] double getInterval() const noexcept override { return interval.load(); }
	[[nodiscard]] unsigned getPingSize() const noexcept override { return packetSize.load(); }
	[[nodiscard]] bool getUseDNS() const noexcept override { return resolveNames.load(); }
	[[nodiscard]] unsigned getMaxHops() const noexcept override { return maxHops.load(); }
	[[nodiscard]] unsigned getTimeoutMs() const noexcept override { return timeoutMs.load(); }
	[[nodiscard]] unsigned getCycles() const noexcept override { return cycles.load(); }
	[[nodiscard]] unsigned getTos() const noexcept override { return tos.load(); }
	[[nodiscard]] int getPayloadPattern() const noexcept override { return payloadPattern.load(); }
	[[nodiscard]] unsigned getStartTtl() const noexcept override { return startTtl.load(); }
	[[nodiscard]] unsigned getMinimumTtl() const noexcept override { return minimumTtl.load(); }
	[[nodiscard]] unsigned getUnknownHostLimit() const noexcept override { return unknownHostLimit.load(); }
	[[nodiscard]] unsigned getEcmpDisplayLimit() const noexcept override { return ecmpDisplayLimit.load(); }
	[[nodiscard]] unsigned getReplyCacheSeconds() const noexcept override { return replyCacheSeconds.load(); }
	[[nodiscard]] bool getDontFragment() const noexcept override { return dontFragment.load(); }
	[[nodiscard]] bool getLookupAsnIsp() const noexcept override { return lookupAsnIsp.load(); }
	[[nodiscard]] bool getUseIPv4() const noexcept override { return useIPv4.load(); }
	[[nodiscard]] bool getUseIPv6() const noexcept override { return useIPv6.load(); }
	[[nodiscard]] bool getQueryPublicNetworkInfo() const noexcept override { return queryPublicInfo.load(); }
	void notifyTraceDataChanged() const noexcept override
	{
		// The 100 ms dialog timer coalesces bursts of per-probe events.  Keeping
		// this callback lock-free also prevents the trace scheduler from waiting
		// on a busy UI thread.
		traceDataDirty.store(true, std::memory_order_release);
	}

protected:
	void DoDataExchange(CDataExchange* dataExchange) override;
	BOOL OnInitDialog() override;
	void OnCancel() override;
	BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result) override;

private:
	static constexpr UINT_PTR dialogTimerId = 1;
	static constexpr UINT dialogTimerMs = 100;
	static constexpr UINT messageTraceFinished = WM_APP + 20;
	static constexpr UINT messageNetworkInfoReady = WM_APP + 21;
	static constexpr UINT messageTraceDataChanged = WM_APP + 22;
	static constexpr UINT messageNetworkInterfaceChanged = WM_APP + 23;

	enum class DisplayRowKind { primary, responder, unknown_range };
	struct DisplayRow final {
		DisplayRowKind kind = DisplayRowKind::primary;
		unsigned firstHop = 0;
		unsigned lastHop = 0;
		size_t responderIndex = 0;
		std::wstring responderAddress;
	};

	CButton buttonOptions;
	CButton buttonStart;
	CButton buttonReset;
	CButton buttonScreenshot;
	CButton buttonCopyExport;
	CButton buttonNetworkDetails;
	CComboBox comboHost;
	CListCtrl listMtr;
	CStatic groupTarget;
	CStatic groupActions;
	CStatic groupPublicInfo;
	CMFCLinkCtrl companyLink;
	CFont tableFont;
	CFont technicalFont;
	std::vector<DisplayRow> displayRows;
	bool listIsVisible = false;
	bool firstDataResize = true;
	std::size_t lastAutoRowCount = 0;
	int naturalColumnsWidth = 0;
	std::array<int, 14> automaticColumnWidths{};
	bool userSizedColumns = false;
	bool userSizedWindow = false;
	bool publicInfoAutoSized = false;
	int topContentBottom = 0;

	std::wstring defaultHostname;
	std::wstring currentTarget;
	std::shared_ptr<WinMTRNet> wmtrnet;
	std::mutex tracerMutex;
	std::optional<std::jthread> traceThread;
	std::atomic_bool tracing = false;
	mutable std::atomic_bool traceDataDirty = false;
	std::atomic_uint64_t traceGeneration = 0;
	std::uint64_t lastRenderedRevision = ~std::uint64_t{};
	STATES state = STATES::IDLE;
	HICON icon = nullptr;

	std::atomic<double> interval;
	std::atomic_uint packetSize;
	std::atomic_uint maxHops;
	std::atomic_uint timeoutMs;
	std::atomic_uint cycles;
	std::atomic_uint tos;
	std::atomic_int payloadPattern;
	std::atomic_uint startTtl;
	std::atomic_uint minimumTtl;
	std::atomic_uint unknownHostLimit;
	std::atomic_uint ecmpDisplayLimit;
	std::atomic_uint replyCacheSeconds;
	std::atomic_bool resolveNames;
	std::atomic_bool lookupAsnIsp;
	std::atomic_bool dontFragment;
	std::atomic_bool useIPv4;
	std::atomic_bool useIPv6;
	std::atomic_bool queryPublicInfo;
	std::atomic_uint publicInfoRefreshMode;
	std::atomic_uint publicInfoRefreshMinutes;
	unsigned historyLimit;
	unsigned persistentHistoryLimit = WinMTRUtils::DEFAULT_MAX_LRU;
	std::vector<std::wstring> sessionHistory;
	int historyCount = 0;
	bool autoStart = false;
	bool hasPacketSizeFromCommandLine = false;
	bool hasHistoryLimitFromCommandLine = false;
	bool hasIntervalFromCommandLine = false;
	bool hasResolveNamesFromCommandLine = false;
	bool hasMaxHopsFromCommandLine = false;
	bool hasTimeoutFromCommandLine = false;
	bool hasCyclesFromCommandLine = false;
	bool hasTosFromCommandLine = false;
	bool hasPayloadPatternFromCommandLine = false;
	bool hasStartTtlFromCommandLine = false;
	bool hasMinimumTtlFromCommandLine = false;
	bool hasUnknownHostLimitFromCommandLine = false;
	bool hasEcmpDisplayLimitFromCommandLine = false;
	bool hasReplyCacheFromCommandLine = false;
	bool hasLookupAsnIspFromCommandLine = false;
	bool hasDontFragmentFromCommandLine = false;
	bool hasAddressFamiliesFromCommandLine = false;
	bool hasQueryPublicInfoFromCommandLine = false;
	bool hasPublicInfoRefreshFromCommandLine = false;

	std::mutex networkInfoMutex;
	winmtr::network_data::CurrentNetworkInfo networkInfo;
	std::optional<std::jthread> networkInfoThread;
	std::atomic_uint64_t networkInfoGeneration = 0;
	std::atomic_bool networkInfoRunning = false;
	std::atomic_bool networkInfoRestartPending = false;
	HANDLE networkInterfaceNotification = nullptr;
	std::uint64_t lastNetworkInfoQueryTick = 0;
	std::uint64_t lastNetworkChangeRefreshTick = 0;

	void pingThread(std::stop_token stopToken, std::wstring host,
		std::uint64_t generation) noexcept;
	void stopTrace() noexcept;
	void startNetworkInfoQuery();
	void stopNetworkInfoQuery() noexcept;
	void configureNetworkInterfaceNotification() noexcept;
	static VOID CALLBACK NetworkInterfaceChangeCallback(PVOID context,
		PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE notificationType) noexcept;
	void updateNetworkInfoSummary();
	void showNetworkInfoDialog();
	void setStatus(const wchar_t* text);
	void ClearHistory();
	void SaveSettings() noexcept;
	void AddHostToHistory(const std::wstring& host);
	void configureList();
	void applyTechnicalFonts(UINT dpi);
	void adjustWindowForRows();
	void resizeWindowToContent();
	[[nodiscard]] CSize minimumWindowSize();
	void layoutControls(int clientWidth, int clientHeight);
	void showNodeDetails(const DisplayRow& row);
	[[nodiscard]] bool confirmShare() const;
	void showCopyExportMenu();
	void copyText();
	void copyHtml();
	void exportText();
	void exportHtml();
	void exportCsv();
	void exportJson();
	void screenshotToClipboard();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT controlType);
	afx_msg void OnSize(UINT type, int width, int height);
	afx_msg void OnSizing(UINT side, LPRECT rect);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
	afx_msg LRESULT OnDpiChanged(WPARAM newDpi, LPARAM suggestedRect);
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnRestart() noexcept;
	afx_msg void OnOptions();
	afx_msg void OnResetStatistics();
	afx_msg void OnScreenshot();
	afx_msg void OnCopyExport();
	afx_msg void OnNetworkDetails();
	afx_msg void OnExportCopyText();
	afx_msg void OnExportCopyHtml();
	afx_msg void OnExportSaveText();
	afx_msg void OnExportSaveHtml();
	afx_msg void OnExportSaveCsv();
	afx_msg void OnExportSaveJson();
	afx_msg void OnDblclkList(NMHDR* header, LRESULT* result);
	afx_msg void OnCbnSelchangeComboHost();
	afx_msg void OnCbnSelendokComboHost();
	afx_msg void OnCbnCloseupComboHost();
	afx_msg void OnTimer(UINT_PTR timerId) noexcept;
	afx_msg void OnClose();
	afx_msg LRESULT OnTraceFinished(WPARAM result, LPARAM errorCode);
	afx_msg LRESULT OnNetworkInfoReady(WPARAM generation, LPARAM unused);
	afx_msg LRESULT OnTraceDataChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnNetworkInterfaceChanged(WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP()
};
