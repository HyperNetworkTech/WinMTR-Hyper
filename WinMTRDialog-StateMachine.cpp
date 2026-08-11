module;

#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <afx.h>
#include <afxext.h>
#include <afxdisp.h>
#include "resource.h"

module WinMTR.Dialog:StateMachine;

import :ClassDef;
import <mutex>;
import <string>;
import WinMTRUtils;

namespace {
[[nodiscard]] CString localized(UINT id)
{
	CString value;
	value.LoadStringW(id);
	return value;
}

void hideComboEditSelection(CComboBox& combo, bool hide)
{
	COMBOBOXINFO info{ .cbSize = sizeof(COMBOBOXINFO) };
	if (!GetComboBoxInfo(combo.GetSafeHwnd(), &info) || info.hwndItem == nullptr) return;
	SendMessageW(info.hwndItem, EM_SETSEL, 0, 0);
	SendMessageW(info.hwndItem, EM_HIDESELECTION, hide ? TRUE : FALSE, FALSE);
	RedrawWindow(info.hwndItem, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}
}

void WinMTRDialog::Transit(STATES newState)
{
	if (newState == STATES::TRACING && state != STATES::IDLE) return;
	if (newState == STATES::STOPPING && state != STATES::TRACING) return;

	switch (newState) {
	case STATES::TRACING: {
		state = STATES::TRACING;
		firstDataResize = true;
		buttonStart.SetWindowTextW(localized(IDS_STRING_STOP));
		buttonStart.EnableWindow(TRUE);
		buttonStart.SetFocus();
		comboHost.SetEditSel(0, 0);
		comboHost.EnableWindow(FALSE);
		hideComboEditSelection(comboHost, true);
		buttonOptions.EnableWindow(FALSE);
		tracing.store(true, std::memory_order_release);
		const auto generation = ++traceGeneration;
		std::scoped_lock lock(tracerMutex);
		if (traceThread) traceThread.reset();
		traceThread.emplace([this, target = currentTarget, generation](std::stop_token token) mutable {
			pingThread(token, std::move(target), generation);
		});
		break;
	}
	case STATES::STOPPING:
		state = STATES::STOPPING;
		buttonStart.EnableWindow(FALSE);
		comboHost.EnableWindow(FALSE);
		buttonOptions.EnableWindow(FALSE);
		setStatus(localized(IDS_STATUS_WAITING_PACKETS));
		stopTrace();
		DisplayRedraw();
		break;
	case STATES::IDLE: {
		if (state == STATES::EXIT) return;
		state = STATES::IDLE;
		{
			std::scoped_lock lock(tracerMutex);
			if (traceThread && !tracing.load(std::memory_order_acquire)) traceThread.reset();
		}
		buttonStart.SetWindowTextW(localized(IDS_STRING_START));
		buttonStart.EnableWindow(TRUE);
		comboHost.EnableWindow(TRUE);
		hideComboEditSelection(comboHost, false);
		comboHost.SetEditSel(0, 0);
		buttonOptions.EnableWindow(TRUE);
		setStatus(localized(IDS_STATUS_READY));
		comboHost.SetFocus();
		break;
	}
	case STATES::EXIT:
		state = STATES::EXIT;
		buttonStart.EnableWindow(FALSE);
		comboHost.EnableWindow(FALSE);
		buttonOptions.EnableWindow(FALSE);
		stopTrace();
		stopNetworkInfoQuery();
		if (!tracing.load(std::memory_order_acquire) && !wmtrnet->isTracing()) CDialog::OnOK();
		else setStatus(localized(IDS_STATUS_WAITING_PACKETS));
		break;
	}
}

void WinMTRDialog::OnTimer(UINT_PTR timerId) noexcept
{
	static unsigned redrawDivider = 0;
	if (timerId == dialogTimerId) {
		const bool hasTraceEvent = traceDataDirty.exchange(false, std::memory_order_acq_rel);
		if (hasTraceEvent
			|| ((state == STATES::TRACING || state == STATES::STOPPING || listIsVisible)
				&& ++redrawDivider % 5 == 0)) {
			DisplayRedraw();
		}
		const bool active = tracing.load(std::memory_order_acquire) || wmtrnet->isTracing();
		if (!active) {
			if (state == STATES::EXIT) CDialog::OnOK();
			else if (state == STATES::TRACING || state == STATES::STOPPING) Transit(STATES::IDLE);
		}
		if (queryPublicInfo.load()
			&& publicInfoRefreshMode.load() == WinMTRUtils::PUBLIC_INFO_REFRESH_FIXED_INTERVAL
			&& !networkInfoRunning.load()) {
			const std::uint64_t now = GetTickCount64();
			const std::uint64_t intervalMilliseconds =
				static_cast<std::uint64_t>(publicInfoRefreshMinutes.load()) * 60'000u;
			if (lastNetworkInfoQueryTick == 0
				|| now < lastNetworkInfoQueryTick
				|| now - lastNetworkInfoQueryTick >= intervalMilliseconds) {
				startNetworkInfoQuery();
			}
		}
	}
	CDialog::OnTimer(timerId);
}

LRESULT WinMTRDialog::OnTraceFinished(WPARAM generation, LPARAM errorCode)
{
	if (generation != traceGeneration.load(std::memory_order_acquire)) return 0;
	DisplayRedraw();
	if (errorCode == WSAHOST_NOT_FOUND && state != STATES::EXIT) {
		AfxMessageBox(IDS_STRING_UNABLE_TO_RESOLVE_HOSTNAME, MB_ICONERROR);
	}
	else if (errorCode != ERROR_SUCCESS && errorCode != ERROR_CANCELLED && state != STATES::EXIT) {
		AfxMessageBox(IDS_ERROR_TRACE_FAILED, MB_OK | MB_ICONERROR);
	}
	if (state == STATES::EXIT) {
		CDialog::OnOK();
	}
	else if (state == STATES::TRACING || state == STATES::STOPPING) {
		Transit(STATES::IDLE);
	}
	return 0;
}

LRESULT WinMTRDialog::OnTraceDataChanged(WPARAM, LPARAM)
{
	traceDataDirty.store(true, std::memory_order_release);
	return 0;
}

LRESULT WinMTRDialog::OnNetworkInterfaceChanged(WPARAM, LPARAM)
{
	if (!queryPublicInfo.load()
		|| publicInfoRefreshMode.load() != WinMTRUtils::PUBLIC_INFO_REFRESH_ON_NETWORK_CHANGE
		|| state == STATES::EXIT) return 0;

	constexpr std::uint64_t debounceMilliseconds = 5'000u;
	const std::uint64_t now = GetTickCount64();
	if (lastNetworkChangeRefreshTick != 0
		&& now >= lastNetworkChangeRefreshTick
		&& now - lastNetworkChangeRefreshTick < debounceMilliseconds) return 0;
	lastNetworkChangeRefreshTick = now;
	startNetworkInfoQuery();
	return 0;
}

void WinMTRDialog::OnClose()
{
	Transit(STATES::EXIT);
}
