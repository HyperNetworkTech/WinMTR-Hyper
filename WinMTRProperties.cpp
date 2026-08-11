/*
WinMTR
Copyright (C) 2010-2019 Appnor MSP S.A.
Copyright (C) 2019-2025 Leetsoftwerx

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.
*/

#include "WinMTRGlobal.h"
#include "WinMTRProperties.h"
#include "resource.h"
#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

BEGIN_MESSAGE_MAP(WinMTRProperties, CDialog)
	ON_WM_VSCROLL()
	ON_WM_HSCROLL()
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

WinMTRProperties::WinMTRProperties(CWnd* pParent) noexcept
	: CDialog(WinMTRProperties::IDD, pParent)
{
}

void WinMTRProperties::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_PHOST, editHost);
	DDX_Control(pDX, IDC_EDIT_PIP, editIP);
	DDX_Control(pDX, IDC_EDIT_PCOMMENT, editComment);
	DDX_Control(pDX, IDC_EDIT_PCOUNTRY, editCountry);
	DDX_Control(pDX, IDC_EDIT_PASN, editAsn);
	DDX_Control(pDX, IDC_EDIT_PISP, editIsp);
	DDX_Control(pDX, IDC_EDIT_PSENT, editSent);
	DDX_Control(pDX, IDC_EDIT_PRECV, editReceived);
	DDX_Control(pDX, IDC_EDIT_PLOSS, editLoss);
	DDX_Control(pDX, IDC_EDIT_PLAST, editLast);
	DDX_Control(pDX, IDC_EDIT_PBEST, editBest);
	DDX_Control(pDX, IDC_EDIT_PAVRG, editAverage);
	DDX_Control(pDX, IDC_EDIT_PWORST, editWorst);
}

BOOL WinMTRProperties::OnInitDialog()
{
	CDialog::OnInitDialog();

	editHost.SetWindowTextW(host.c_str());
	editIP.SetWindowTextW(ip.c_str());
	editComment.SetWindowTextW(comment.c_str());
	editCountry.SetWindowTextW(country.c_str());
	editAsn.SetWindowTextW(asn.c_str());
	editIsp.SetWindowTextW(isp.c_str());

	CString lossText;
	CString sentText;
	CString receivedText;
	CString lastText;
	CString bestText;
	CString averageText;
	CString worstText;
	lossText.Format(L"%d%%", pck_loss);
	sentText.Format(L"%d", pck_sent);
	receivedText.Format(L"%d", pck_recv);
	lastText.Format(L"%.1f", static_cast<double>(ping_last));
	bestText.Format(L"%.1f", static_cast<double>(ping_best));
	averageText.Format(L"%.1f", static_cast<double>(ping_avrg));
	worstText.Format(L"%.1f", static_cast<double>(ping_worst));

	editLoss.SetWindowTextW(lossText);
	editSent.SetWindowTextW(sentText);
	editReceived.SetWindowTextW(receivedText);
	editLast.SetWindowTextW(lastText);
	editBest.SetWindowTextW(bestText);
	editAverage.SetWindowTextW(averageText);
	editWorst.SetWindowTextW(worstText);

	ApplyTechnicalFont();
	ConfigureResponsiveLayout();
	ConfigureVerticalScrolling();
	return TRUE;
}

void WinMTRProperties::ApplyTechnicalFont()
{
	if (!technicalFont.GetSafeHandle() &&
		technicalFont.CreatePointFont(90, WinMTRBranding::table_font.data())) {
		CEdit* technicalEdits[] = {
			&editHost, &editIP, &editAsn, &editIsp,
			&editSent, &editReceived, &editLoss, &editLast,
			&editBest, &editAverage, &editWorst
		};
		for (CEdit* edit : technicalEdits) {
			edit->SetFont(&technicalFont);
		}
	}
}

void WinMTRProperties::MoveControlDlu(int id, int x, int y, int width, int height)
{
	CRect rect(x, y, x + width, y + height);
	MapDialogRect(&rect);
	if (auto* control = GetDlgItem(id)) {
		control->SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void WinMTRProperties::ConfigureResponsiveLayout()
{
	CRect windowRect;
	GetWindowRect(windowRect);
	MONITORINFO monitor{ .cbSize = sizeof(MONITORINFO) };
	GetMonitorInfoW(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
	const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
	if (windowRect.Width() <= workWidth) return;

	constexpr int clientWidthDlu = 245;
	constexpr int clientHeightDlu = 347;
	MoveControlDlu(IDC_PROPERTIES_GROUP_NODE, 7, 7, 231, 111);
	constexpr int nodeLabels[] = {
		IDC_PROPERTIES_LABEL_HOST, IDC_PROPERTIES_LABEL_IP,
		IDC_PROPERTIES_LABEL_COUNTRY, IDC_PROPERTIES_LABEL_ASN,
		IDC_PROPERTIES_LABEL_ISP, IDC_PROPERTIES_LABEL_COMMENT
	};
	constexpr int nodeEdits[] = {
		IDC_EDIT_PHOST, IDC_EDIT_PIP, IDC_EDIT_PCOUNTRY,
		IDC_EDIT_PASN, IDC_EDIT_PISP, IDC_EDIT_PCOMMENT
	};
	for (int index = 0; index < 6; ++index) {
		MoveControlDlu(nodeLabels[index], 15, 20 + index * 16, 40, 11);
		MoveControlDlu(nodeEdits[index], 60, 18 + index * 16, 174, 14);
	}

	MoveControlDlu(IDC_PROPERTIES_GROUP_STATS, 7, 124, 231, 82);
	constexpr int statisticLabels[] = {
		IDC_PROPERTIES_LABEL_SENT, IDC_PROPERTIES_LABEL_RECEIVED,
		IDC_PROPERTIES_LABEL_LOSS
	};
	constexpr int statisticEdits[] = { IDC_EDIT_PSENT, IDC_EDIT_PRECV, IDC_EDIT_PLOSS };
	for (int index = 0; index < 3; ++index) {
		MoveControlDlu(statisticLabels[index], 15, 140 + index * 19, 68, 11);
		MoveControlDlu(statisticEdits[index], 91, 138 + index * 19, 143, 14);
	}

	MoveControlDlu(IDC_PROPERTIES_GROUP_LATENCY, 7, 212, 231, 99);
	constexpr int latencyLabels[] = {
		IDC_PROPERTIES_LABEL_LAST, IDC_PROPERTIES_LABEL_BEST,
		IDC_PROPERTIES_LABEL_AVERAGE, IDC_PROPERTIES_LABEL_WORST
	};
	constexpr int latencyEdits[] = {
		IDC_EDIT_PLAST, IDC_EDIT_PBEST, IDC_EDIT_PAVRG, IDC_EDIT_PWORST
	};
	for (int index = 0; index < 4; ++index) {
		MoveControlDlu(latencyLabels[index], 15, 228 + index * 19, 90, 11);
		MoveControlDlu(latencyEdits[index], 111, 226 + index * 19, 123, 14);
	}
	MoveControlDlu(IDOK, 94, 321, 56, 18);

	CRect desiredClient(0, 0, clientWidthDlu, clientHeightDlu);
	MapDialogRect(&desiredClient);
	CRect oldClient;
	GetClientRect(oldClient);
	SetWindowPos(nullptr, 0, 0,
		desiredClient.Width() + windowRect.Width() - oldClient.Width(),
		desiredClient.Height() + windowRect.Height() - oldClient.Height(),
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WinMTRProperties::ConfigureVerticalScrolling()
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

void WinMTRProperties::ScrollTo(int position)
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

void WinMTRProperties::ScrollToHorizontal(int position)
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

void WinMTRProperties::OnVScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar)
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

void WinMTRProperties::OnHScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar)
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

BOOL WinMTRProperties::OnMouseWheel(UINT flags, short delta, CPoint point)
{
	if (scrollMaximum == 0) return CDialog::OnMouseWheel(flags, delta, point);
	ScrollTo(scrollPosition - (delta / WHEEL_DELTA) * 60);
	return TRUE;
}
