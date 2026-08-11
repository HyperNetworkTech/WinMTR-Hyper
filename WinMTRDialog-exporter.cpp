module;

#pragma warning(disable : 4005)
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <afx.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcmn.h>
#include "resource.h"
#include "WinMTRBranding.h"

module WinMTR.Dialog:exporter;

import :ClassDef;
import <algorithm>;
import <array>;
import <cstdint>;
import <format>;
import <iterator>;
import <optional>;
import <sstream>;
import <string>;
import <string_view>;
import <vector>;
import <cstring>;
import WinMTRIPUtils;
import WinMTRSNetHost;

namespace {

[[nodiscard]] CString localized(UINT id)
{
	CString value;
	value.LoadStringW(id);
	return value;
}

[[nodiscard]] std::array<std::wstring, 14> exportHeaders()
{
	constexpr std::array<UINT, 14> ids{
		IDS_COLUMN_HOST, IDS_COLUMN_HOP, IDS_COLUMN_LOSS, IDS_COLUMN_SENT, IDS_COLUMN_RECEIVED,
		IDS_COLUMN_BEST, IDS_COLUMN_AVERAGE, IDS_COLUMN_WORST, IDS_COLUMN_LAST, IDS_COLUMN_JITTER,
		IDS_COLUMN_STDDEV, IDS_COLUMN_COUNTRY, IDS_COLUMN_ASN, IDS_COLUMN_ISP
	};
	std::array<std::wstring, 14> result;
	for (size_t index = 0; index < ids.size(); ++index) {
		result[index] = localized(ids[index]).GetString();
	}
	return result;
}

struct ExportRow final {
	std::array<std::wstring, 14> cells;
	bool responder = false;
};

[[nodiscard]] std::string toUtf8(std::wstring_view value)
{
	if (value.empty()) return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		nullptr, 0, nullptr, nullptr);
	if (length <= 0) return {};
	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length,
		nullptr, nullptr);
	return result;
}

[[nodiscard]] std::wstring primaryHost(const s_nethost& hop)
{
	auto name = hop.getName();
	if (name.empty() && !hop.responders.empty()) name = hop.responders.front().getName();
	return name.empty() ? std::wstring(localized(IDS_STRING_NO_RESPONSE_FROM_HOST).GetString()) : name;
}

[[nodiscard]] std::vector<ExportRow> makeRows(const WinMTRTraceSnapshot& snapshot)
{
	std::vector<ExportRow> rows;
	for (const auto& hop : snapshot.hops) {
		ExportRow row;
		row.cells[0] = primaryHost(hop);
		row.cells[1] = std::to_wstring(hop.hop);
		row.cells[2] = std::format(L"{:.0f}%", hop.getLossPercent());
		row.cells[3] = std::to_wstring(hop.xmit);
		row.cells[4] = std::to_wstring(hop.returned);
		if (hop.returned != 0) {
			row.cells[5] = std::to_wstring(hop.best);
			row.cells[6] = std::format(L"{:.1f}", hop.getAverageMs());
			row.cells[7] = std::to_wstring(hop.worst);
			row.cells[8] = std::to_wstring(hop.last);
			row.cells[9] = std::format(L"{:.1f}", hop.jitter);
			row.cells[10] = std::format(L"{:.1f}", hop.stddev);
		}
		row.cells[11] = hop.country;
		row.cells[12] = hop.asn;
		row.cells[13] = hop.isp;
		rows.emplace_back(std::move(row));

		for (size_t index = 1; index < hop.responders.size(); ++index) {
			const auto& responder = hop.responders[index];
			ExportRow alternative;
			alternative.responder = true;
			alternative.cells[0] = L"  + " + responder.getName();
			alternative.cells[1] = std::to_wstring(hop.hop);
			alternative.cells[11] = responder.country;
			alternative.cells[12] = responder.asn;
			alternative.cells[13] = responder.isp;
			rows.emplace_back(std::move(alternative));
		}
	}
	return rows;
}

[[nodiscard]] std::wstring serializeText(const WinMTRTraceSnapshot& snapshot)
{
	const auto headers = exportHeaders();
	const CString targetLabel = localized(IDS_EXPORT_TARGET_LABEL);
	std::wostringstream out;
	out << targetLabel.GetString() << L"：" << snapshot.target << L"\r\n";
	for (size_t index = 0; index < headers.size(); ++index) {
		if (index != 0) out << L'\t';
		out << headers[index];
	}
	out << L"\r\n";
	for (const auto& row : makeRows(snapshot)) {
		for (size_t index = 0; index < row.cells.size(); ++index) {
			if (index != 0) out << L'\t';
			out << row.cells[index];
		}
		out << L"\r\n";
	}
	return out.str();
}

[[nodiscard]] std::wstring htmlEscape(std::wstring_view value)
{
	std::wstring result;
	for (const wchar_t ch : value) {
		switch (ch) {
		case L'&': result += L"&amp;"; break;
		case L'<': result += L"&lt;"; break;
		case L'>': result += L"&gt;"; break;
		case L'"': result += L"&quot;"; break;
		case L'\'': result += L"&#39;"; break;
		default: result.push_back(ch); break;
		}
	}
	return result;
}

[[nodiscard]] std::wstring serializeHtml(const WinMTRTraceSnapshot& snapshot, bool completeDocument)
{
	const auto headers = exportHeaders();
	const CString reportTitle = localized(IDS_EXPORT_REPORT_TITLE);
	const CString targetLabel = localized(IDS_EXPORT_TARGET_LABEL);
	std::wostringstream out;
	if (completeDocument) {
		out << L"<!doctype html><html lang=\"zh-Hant-TW\"><head><meta charset=\"utf-8\">"
			L"<title>" << htmlEscape(reportTitle.GetString()) << L"</title>"
			L"<style>body{font-family:'" << WinMTRBranding::ui_font << L"',sans-serif}"
			L"table{border-collapse:collapse;font-family:'" << WinMTRBranding::table_font << L"',monospace;font-size:13px}"
			L"th,td{border:1px solid #888;padding:3px 7px;white-space:pre}th{background:#eee}"
			L"tbody tr:nth-child(even){background:#f6f6f6}</style></head><body>";
	}
	out << L"<h1>" << htmlEscape(reportTitle.GetString()) << L"</h1><p>"
		<< htmlEscape(targetLabel.GetString()) << L"：<code>" << htmlEscape(snapshot.target)
		<< L"</code></p><table><thead><tr>";
	for (const auto header : headers) out << L"<th>" << header << L"</th>";
	out << L"</tr></thead><tbody>";
	for (const auto& row : makeRows(snapshot)) {
		out << (row.responder ? L"<tr class=\"responder\">" : L"<tr>");
		for (const auto& cell : row.cells) out << L"<td>" << htmlEscape(cell) << L"</td>";
		out << L"</tr>";
	}
	out << L"</tbody></table>";
	if (completeDocument) out << L"</body></html>";
	return out.str();
}

[[nodiscard]] std::wstring csvCell(std::wstring_view value)
{
	if (value.find_first_of(L",\"\r\n") == std::wstring_view::npos) return std::wstring(value);
	std::wstring result = L"\"";
	for (const wchar_t ch : value) {
		if (ch == L'\"') result += L"\"\"";
		else result.push_back(ch);
	}
	result.push_back(L'\"');
	return result;
}

[[nodiscard]] std::wstring serializeCsv(const WinMTRTraceSnapshot& snapshot)
{
	const auto headers = exportHeaders();
	std::wostringstream out;
	for (size_t index = 0; index < headers.size(); ++index) {
		if (index != 0) out << L',';
		out << csvCell(headers[index]);
	}
	out << L"\r\n";
	for (const auto& row : makeRows(snapshot)) {
		for (size_t index = 0; index < row.cells.size(); ++index) {
			if (index != 0) out << L',';
			out << csvCell(row.cells[index]);
		}
		out << L"\r\n";
	}
	return out.str();
}

[[nodiscard]] std::wstring jsonEscape(std::wstring_view value)
{
	std::wstring result;
	for (const wchar_t ch : value) {
		switch (ch) {
		case L'"': result += L"\\\""; break;
		case L'\\': result += L"\\\\"; break;
		case L'\b': result += L"\\b"; break;
		case L'\f': result += L"\\f"; break;
		case L'\n': result += L"\\n"; break;
		case L'\r': result += L"\\r"; break;
		case L'\t': result += L"\\t"; break;
		default:
			if (ch < 0x20) result += std::format(L"\\u{:04x}", static_cast<unsigned>(ch));
			else result.push_back(ch);
			break;
		}
	}
	return result;
}

[[nodiscard]] std::wstring serializeJson(const WinMTRTraceSnapshot& snapshot)
{
	std::wostringstream out;
	out << L"{\r\n  \"target\": \"" << jsonEscape(snapshot.target) << L"\",\r\n  \"hops\": [";
	for (size_t hopIndex = 0; hopIndex < snapshot.hops.size(); ++hopIndex) {
		const auto& hop = snapshot.hops[hopIndex];
		if (hopIndex != 0) out << L',';
		out << L"\r\n    {"
			<< L"\"hop\":" << hop.hop << L','
			<< L"\"host\":\"" << jsonEscape(primaryHost(hop)) << L"\","
			<< L"\"ip\":\"" << jsonEscape(addr_to_string(hop.addr)) << L"\","
			<< L"\"loss_percent\":" << std::format(L"{:.2f}", hop.getLossPercent()) << L','
			<< L"\"sent\":" << hop.xmit << L','
			<< L"\"completed\":" << hop.completed << L','
			<< L"\"received\":" << hop.returned << L','
			<< L"\"timed_out\":" << hop.timed_out << L','
			<< L"\"in_flight\":" << hop.in_flight << L','
			<< L"\"local_errors\":" << hop.local_errors << L','
			<< L"\"scheduler_skipped\":" << hop.scheduler_skipped << L','
			<< L"\"late_completions\":" << hop.late_completions << L','
			<< L"\"best_ms\":" << hop.best << L','
			<< L"\"average_ms\":" << std::format(L"{:.2f}", hop.getAverageMs()) << L','
			<< L"\"worst_ms\":" << hop.worst << L','
			<< L"\"last_ms\":" << hop.last << L','
			<< L"\"jitter_ms\":" << std::format(L"{:.2f}", hop.jitter) << L','
			<< L"\"stddev_ms\":" << std::format(L"{:.2f}", hop.stddev) << L','
			<< L"\"country\":\"" << jsonEscape(hop.country) << L"\","
			<< L"\"asn\":\"" << jsonEscape(hop.asn) << L"\","
			<< L"\"isp\":\"" << jsonEscape(hop.isp) << L"\","
			<< L"\"responders\":[";
		for (size_t responderIndex = 0; responderIndex < hop.responders.size(); ++responderIndex) {
			const auto& responder = hop.responders[responderIndex];
			if (responderIndex != 0) out << L',';
			out << L"{\"host\":\"" << jsonEscape(responder.getName()) << L"\","
				<< L"\"ip\":\"" << jsonEscape(addr_to_string(responder.addr)) << L"\","
				<< L"\"country\":\"" << jsonEscape(responder.country) << L"\","
				<< L"\"asn\":\"" << jsonEscape(responder.asn) << L"\","
				<< L"\"isp\":\"" << jsonEscape(responder.isp) << L"\"}";
		}
		out << L"]}";
	}
	out << L"\r\n  ]\r\n}\r\n";
	return out.str();
}

[[nodiscard]] bool putClipboard(HWND owner, const std::wstring& text,
	const std::optional<std::string>& html = std::nullopt)
{
	if (!OpenClipboard(owner)) return false;
	struct Closer final { ~Closer() { CloseClipboard(); } } closer;
	if (!EmptyClipboard()) return false;
	const auto put = [](UINT format, const void* data, size_t bytes) {
		HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (memory == nullptr) return false;
		void* destination = GlobalLock(memory);
		if (destination == nullptr) { GlobalFree(memory); return false; }
		memcpy(destination, data, bytes);
		GlobalUnlock(memory);
		if (SetClipboardData(format, memory) == nullptr) { GlobalFree(memory); return false; }
		return true;
	};
	if (!put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t))) return false;
	if (html) {
		const UINT format = RegisterClipboardFormatW(L"HTML Format");
		if (format == 0 || !put(format, html->c_str(), html->size() + 1)) return false;
	}
	return true;
}

[[nodiscard]] std::string makeClipboardHtml(const std::wstring& fragment)
{
	const std::string body = "<html lang=\"zh-Hant-TW\"><head><meta charset=\"utf-8\"><style>"
		"body{font-family:'" + toUtf8(WinMTRBranding::ui_font) + "',sans-serif}"
		"table{border-collapse:collapse;font-family:'" + toUtf8(WinMTRBranding::table_font) +
		"',monospace;font-size:13px}th,td{border:1px solid #888;padding:3px 7px;white-space:pre}"
		"th{background:#eee}</style></head><body><!--StartFragment-->" + toUtf8(fragment) +
		"<!--EndFragment--></body></html>";
	constexpr std::string_view prototype =
		"Version:0.9\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\n"
		"StartFragment:0000000000\r\nEndFragment:0000000000\r\n";
	const size_t startHtml = prototype.size();
	const size_t endHtml = startHtml + body.size();
	const size_t startFragment = startHtml + body.find("<!--StartFragment-->") + 20;
	const size_t endFragment = startHtml + body.find("<!--EndFragment-->");
	return std::format("Version:0.9\r\nStartHTML:{:010}\r\nEndHTML:{:010}\r\n"
		"StartFragment:{:010}\r\nEndFragment:{:010}\r\n{}",
		startHtml, endHtml, startFragment, endFragment, body);
}

[[nodiscard]] bool writeUtf8File(const CString& path, std::wstring_view contents, bool bom)
{
	const auto bytes = toUtf8(contents);
	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	struct FileCloser final { HANDLE value; ~FileCloser() { CloseHandle(value); } } closer{ file };
	DWORD written = 0;
	if (bom) {
		constexpr unsigned char marker[]{ 0xef, 0xbb, 0xbf };
		if (!WriteFile(file, marker, sizeof(marker), &written, nullptr) || written != sizeof(marker)) return false;
	}
	return bytes.empty() || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
		&& written == bytes.size());
}

[[nodiscard]] CString makeExportFilter(UINT typeId, const wchar_t* pattern)
{
	CString filter = localized(typeId);
	filter += L"|";
	filter += pattern;
	filter += L"|";
	filter += localized(IDS_FILE_TYPE_ALL);
	filter += L"|*.*||";
	return filter;
}

[[nodiscard]] CString selectExportPath(CWnd* owner, const wchar_t* extension, const CString& filter)
{
	CFileDialog dialog(FALSE, extension, nullptr, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, filter, owner);
	return dialog.DoModal() == IDOK ? dialog.GetPathName() : CString{};
}

} // namespace

bool WinMTRDialog::confirmShare() const
{
	const auto snapshot = wmtrnet->getTraceSnapshot();
	if (snapshot.first_actual_sent >= 100) return true;
	return AfxMessageBox(IDS_SHARE_SAMPLE_WARNING,
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void WinMTRDialog::showCopyExportMenu()
{
	CMenu menu;
	if (!menu.LoadMenuW(IDR_MENU_COPY_EXPORT)) return;
	CMenu* popup = menu.GetSubMenu(0);
	if (popup == nullptr) return;
	CRect buttonRect;
	buttonCopyExport.GetWindowRect(buttonRect);
	popup->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
		buttonRect.left, buttonRect.bottom, this);
}

void WinMTRDialog::copyText()
{
	if (!confirmShare()) return;
	if (!putClipboard(GetSafeHwnd(), serializeText(wmtrnet->getTraceSnapshot()))) {
		AfxMessageBox(IDS_ERROR_CLIPBOARD, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::copyHtml()
{
	if (!confirmShare()) return;
	const auto snapshot = wmtrnet->getTraceSnapshot();
	const auto fragment = serializeHtml(snapshot, false);
	if (!putClipboard(GetSafeHwnd(), serializeText(snapshot), makeClipboardHtml(fragment))) {
		AfxMessageBox(IDS_ERROR_CLIPBOARD, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::exportText()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"txt", makeExportFilter(IDS_FILE_TYPE_TEXT, L"*.txt"));
	if (!path.IsEmpty() && !writeUtf8File(path, serializeText(wmtrnet->getTraceSnapshot()), true)) {
		AfxMessageBox(IDS_ERROR_EXPORT, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::exportHtml()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"html", makeExportFilter(IDS_FILE_TYPE_HTML, L"*.html;*.htm"));
	if (!path.IsEmpty() && !writeUtf8File(path, serializeHtml(wmtrnet->getTraceSnapshot(), true), false)) {
		AfxMessageBox(IDS_ERROR_EXPORT, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::exportCsv()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"csv", makeExportFilter(IDS_FILE_TYPE_CSV, L"*.csv"));
	if (!path.IsEmpty() && !writeUtf8File(path, serializeCsv(wmtrnet->getTraceSnapshot()), true)) {
		AfxMessageBox(IDS_ERROR_EXPORT, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::exportJson()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"json", makeExportFilter(IDS_FILE_TYPE_JSON, L"*.json"));
	if (!path.IsEmpty() && !writeUtf8File(path, serializeJson(wmtrnet->getTraceSnapshot()), false)) {
		AfxMessageBox(IDS_ERROR_EXPORT, MB_OK | MB_ICONERROR);
	}
}

void WinMTRDialog::screenshotToClipboard()
{
	if (!confirmShare()) return;
	CRect windowRect;
	GetWindowRect(windowRect);
	HDC windowDc = ::GetWindowDC(GetSafeHwnd());
	HDC memoryDc = windowDc == nullptr ? nullptr : CreateCompatibleDC(windowDc);
	HBITMAP bitmap = memoryDc == nullptr ? nullptr : CreateCompatibleBitmap(windowDc,
		windowRect.Width(), windowRect.Height());
	HGDIOBJ oldBitmap = bitmap == nullptr ? nullptr : SelectObject(memoryDc, bitmap);
	bool captured = false;
	if (oldBitmap != nullptr && oldBitmap != HGDI_ERROR) {
		captured = ::PrintWindow(GetSafeHwnd(), memoryDc, 0x00000002) != FALSE;
		if (!captured) {
			captured = BitBlt(memoryDc, 0, 0, windowRect.Width(), windowRect.Height(), windowDc,
				0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
		}
		SelectObject(memoryDc, oldBitmap);
	}
	if (memoryDc != nullptr) DeleteDC(memoryDc);
	if (windowDc != nullptr) ::ReleaseDC(GetSafeHwnd(), windowDc);
	bool copied = false;
	if (captured && ::OpenClipboard(GetSafeHwnd())) {
		if (EmptyClipboard() && SetClipboardData(CF_BITMAP, bitmap) != nullptr) {
			copied = true;
			bitmap = nullptr; // Clipboard owns it.
		}
		CloseClipboard();
	}
	if (bitmap != nullptr) DeleteObject(bitmap);
	const auto status = localized(copied ? IDS_STATUS_SCREENSHOT_COPIED : IDS_STATUS_SCREENSHOT_FAILED);
	setStatus(status.GetString());
}
