module;

#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <afx.h>
#include <afxext.h>
#include <afxdisp.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "resource.h"

module WinMTR.Dialog:tracing;

import :ClassDef;
import <algorithm>;
import <mutex>;
import <optional>;
import <string>;
import <vector>;
import WinMTRDnsUtil;
import WinMTRIPUtils;
import WinMTRSNetHost;

bool WinMTRDialog::InitMTRNet() noexcept
{
	if (!useIPv4.load() && !useIPv6.load()) {
		AfxMessageBox(IDS_ERROR_SELECT_IP_VERSION, MB_ICONWARNING);
		return false;
	}
	CString host;
	comboHost.GetWindowTextW(host);
	host.Trim();
	if (host.IsEmpty()) {
		AfxMessageBox(IDS_ERROR_NO_HOST, MB_ICONWARNING);
		comboHost.SetFocus();
		return false;
	}
	return true;
}

void WinMTRDialog::pingThread(std::stop_token stopToken, std::wstring host,
	std::uint64_t generation) noexcept
{
	std::vector<SOCKADDR_INET> candidates;
	for (const auto family : { AF_INET, AF_INET6 }) {
		if ((family == AF_INET && !useIPv4.load()) || (family == AF_INET6 && !useIPv6.load())) continue;
		SOCKADDR_INET selected{};
		INT addressSize = sizeof(selected);
		auto copy = host;
		if (WSAStringToAddressW(copy.data(), family, nullptr,
			reinterpret_cast<LPSOCKADDR>(&selected), &addressSize) == 0) {
			candidates.push_back(selected);
			break;
		}
	}

	if (candidates.empty() && !stopToken.stop_requested()) {
		int family = AF_UNSPEC;
		if (!useIPv4.load()) family = AF_INET6;
		else if (!useIPv6.load()) family = AF_INET;
		const auto addresses = ResolveAddresses(host, family);
		// Prefer IPv4 when both are enabled, then transparently fall back to an
		// IPv6 (or another resolved) address if a complete first path cycle gets
		// no ICMP response at all.  This tests actual trace usability instead of
		// trusting resolver order alone.
		for (const auto preferredFamily : { AF_INET, AF_INET6 }) {
			for (const auto& address : addresses) {
				if (address.si_family != preferredFamily
					|| (preferredFamily == AF_INET && !useIPv4.load())
					|| (preferredFamily == AF_INET6 && !useIPv6.load())) continue;
				const bool duplicate = std::any_of(candidates.begin(), candidates.end(),
					[&](const SOCKADDR_INET& existing) {
						return same_network_address(existing, address);
					});
				if (!duplicate) candidates.push_back(address);
				if (candidates.size() >= 8) break;
			}
			if (candidates.size() >= 8) break;
		}
	}

	if (candidates.empty() || stopToken.stop_requested()) {
		tracing.store(false, std::memory_order_release);
		PostMessageW(messageTraceFinished, static_cast<WPARAM>(generation),
			candidates.empty() ? WSAHOST_NOT_FOUND : ERROR_CANCELLED);
		return;
	}

	try {
		if (candidates.size() == 1) {
			[[maybe_unused]] const auto result = wmtrnet->DoTrace(
				stopToken, candidates.front(), host, false);
		}
		else {
			std::optional<SOCKADDR_INET> firstUsableCandidate;
			bool selected = false;
			for (const auto& candidate : candidates) {
				if (stopToken.stop_requested()) break;
				const auto result = wmtrnet->DoTrace(stopToken, candidate, host, true);
				if (result == WinMTRTraceResult::destination) {
					selected = true;
					break;
				}
				if (result == WinMTRTraceResult::reply && !firstUsableCandidate) {
					firstUsableCandidate = candidate;
				}
			}
			if (!selected && !stopToken.stop_requested()) {
				// Prefer a family that produced any usable ICMP path response.  If
				// every first-cycle attempt was silent, retain resolver preference.
				const auto selectedCandidate = firstUsableCandidate.value_or(candidates.front());
				[[maybe_unused]] const auto result = wmtrnet->DoTrace(
					stopToken, selectedCandidate, host, false);
			}
		}
	}
	catch (...) {
		tracing.store(false, std::memory_order_release);
		PostMessageW(messageTraceFinished, static_cast<WPARAM>(generation), ERROR_GEN_FAILURE);
		return;
	}
	tracing.store(false, std::memory_order_release);
	PostMessageW(messageTraceFinished, static_cast<WPARAM>(generation), ERROR_SUCCESS);
}

void WinMTRDialog::stopTrace() noexcept
{
	std::scoped_lock lock(tracerMutex);
	if (traceThread) traceThread->request_stop();
}
