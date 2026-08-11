/*
WinMTR
Copyright (C) 2010-2019 Appnor MSP S.A.
Copyright (C) 2019-2025 Leetsoftwerx

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.
*/

#pragma once
#ifndef WINMTRPROPERTIES_H_
#define WINMTRPROPERTIES_H_

#pragma warning(disable : 4005)
#include <string>

#include "resource.h"
#include "WinMTRBranding.h"

class WinMTRProperties final : public CDialog
{
public:
	explicit WinMTRProperties(CWnd* pParent = nullptr) noexcept;

	enum { IDD = IDD_DIALOG_PROPERTIES };

	std::wstring host;
	std::wstring ip;
	std::wstring comment;
	std::wstring country;
	std::wstring asn;
	std::wstring isp;

	float ping_last = 0.0F;
	float ping_best = 0.0F;
	float ping_avrg = 0.0F;
	float ping_worst = 0.0F;

	int pck_sent = 0;
	int pck_recv = 0;
	int pck_loss = 0;

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	afx_msg void OnVScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar);
	afx_msg void OnHScroll(UINT scrollCode, UINT position, CScrollBar* scrollBar);
	afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);

	DECLARE_MESSAGE_MAP()

private:
	CEdit editHost;
	CEdit editIP;
	CEdit editComment;
	CEdit editCountry;
	CEdit editAsn;
	CEdit editIsp;
	CEdit editSent;
	CEdit editReceived;
	CEdit editLoss;
	CEdit editLast;
	CEdit editBest;
	CEdit editAverage;
	CEdit editWorst;
	CFont technicalFont;
	int scrollPosition = 0;
	int scrollMaximum = 0;
	int horizontalScrollPosition = 0;
	int horizontalScrollMaximum = 0;

	void ApplyTechnicalFont();
	void ConfigureResponsiveLayout();
	void MoveControlDlu(int id, int x, int y, int width, int height);
	void ConfigureVerticalScrolling();
	void ScrollTo(int position);
	void ScrollToHorizontal(int position);
};

#endif // WINMTRPROPERTIES_H_
