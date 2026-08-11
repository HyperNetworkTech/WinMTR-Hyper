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
import <cstddef>;
import <mutex>;
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
	int resolutionError = WSAHOST_NOT_FOUND;
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
		const auto resolution = ResolveAddressesWithDeadline(host, family, stopToken);
		resolutionError = resolution.error_code;
		std::vector<SOCKADDR_INET> ipv4;
		std::vector<SOCKADDR_INET> ipv6;
		for (const auto& address : resolution.addresses) {
			if (address.si_family == AF_INET && useIPv4.load()) ipv4.push_back(address);
			else if (address.si_family == AF_INET6 && useIPv6.load()) ipv6.push_back(address);
		}
		// Start with IPv4 for compatibility, but interleave the families so the
		// 250 ms candidate race does not make all AAAA records wait behind every A.
		for (std::size_t index = 0; candidates.size() < 8
			&& (index < ipv4.size() || index < ipv6.size()); ++index) {
			if (index < ipv4.size()) candidates.push_back(ipv4[index]);
			if (candidates.size() < 8 && index < ipv6.size()) candidates.push_back(ipv6[index]);
		}
	}

	if (candidates.empty() || stopToken.stop_requested()) {
		tracing.store(false, std::memory_order_release);
		PostMessageW(messageTraceFinished, static_cast<WPARAM>(generation),
			stopToken.stop_requested() ? ERROR_CANCELLED : resolutionError);
		return;
	}

	try {
		const auto selectedCandidate = wmtrnet->SelectCandidate(stopToken, candidates);
		if (!selectedCandidate) {
			tracing.store(false, std::memory_order_release);
			PostMessageW(messageTraceFinished, static_cast<WPARAM>(generation), ERROR_CANCELLED);
			return;
		}
		[[maybe_unused]] const auto result = wmtrnet->DoTrace(
			stopToken, *selectedCandidate, host, false);
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
