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
#include "WinMTRSerialization.h"

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
import <utility>;
import <vector>;
import <cstring>;
import WinMTRIPUtils;
import WinMTRSNetHost;

namespace {

using winmtr::serialization::csvCell;
using winmtr::serialization::jsonEscape;

[[nodiscard]] CString localized(UINT id)
{
	CString value;
	value.LoadStringW(id);
	return value;
}

[[nodiscard]] constexpr std::wstring_view outcomeName(
	WinMTRProbeOutcome outcome) noexcept
{
	switch (outcome) {
	case WinMTRProbeOutcome::none: return L"none";
	case WinMTRProbeOutcome::in_flight: return L"in_flight";
	case WinMTRProbeOutcome::echo_reply: return L"echo_reply";
	case WinMTRProbeOutcome::ttl_expired: return L"ttl_expired";
	case WinMTRProbeOutcome::destination_unreachable: return L"destination_unreachable";
	case WinMTRProbeOutcome::packet_too_big: return L"packet_too_big";
	case WinMTRProbeOutcome::icmp_error: return L"icmp_error";
	case WinMTRProbeOutcome::timeout: return L"timeout";
	case WinMTRProbeOutcome::local_error: return L"local_error";
	case WinMTRProbeOutcome::cancelled: return L"cancelled";
	case WinMTRProbeOutcome::scheduler_skipped: return L"scheduler_skipped";
	case WinMTRProbeOutcome::cached: return L"cached";
	case WinMTRProbeOutcome::late_discarded: return L"late_discarded";
	case WinMTRProbeOutcome::post_destination_discarded:
		return L"post_destination_discarded";
	}
	return L"none";
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
	std::wstring kind = L"hop";
	std::wstring ip;
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

[[nodiscard]] std::wstring isoUtcTimestamp(std::uint64_t unixMilliseconds)
{
	if (unixMilliseconds == 0) return {};
	constexpr std::uint64_t windowsEpochOffset = 116'444'736'000'000'000ull;
	ULARGE_INTEGER ticks{};
	ticks.QuadPart = windowsEpochOffset + unixMilliseconds * 10'000ull;
	FILETIME fileTime{ ticks.LowPart, ticks.HighPart };
	SYSTEMTIME utc{};
	if (!FileTimeToSystemTime(&fileTime, &utc)) return {};
	return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
		utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
		utc.wMilliseconds);
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
		row.ip = addr_to_string(hop.addr);
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
			alternative.kind = L"responder";
			alternative.ip = addr_to_string(responder.addr);
			alternative.responder = true;
			alternative.cells[0] = L"  + " + responder.getName();
			alternative.cells[1] = std::to_wstring(hop.hop);
			alternative.cells[4] = std::to_wstring(responder.hit_count);
			alternative.cells[5] = std::to_wstring(responder.best_ms);
			alternative.cells[6] = std::format(L"{:.1f}", responder.getAverageMs());
			alternative.cells[7] = std::to_wstring(responder.worst_ms);
			alternative.cells[8] = std::to_wstring(responder.last_ms);
			alternative.cells[9] = std::format(L"{:.1f}", responder.jitter_ms);
			alternative.cells[10] = std::format(L"{:.1f}", responder.stddev_ms);
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
	out << targetLabel.GetString() << L"：" << snapshot.target << L"\r\n"
		<< L"Started UTC\t" << isoUtcTimestamp(snapshot.started_at_unix_ms) << L"\r\n"
		<< L"Ended UTC\t";
	if (snapshot.ended_at_unix_ms != 0) {
		out << isoUtcTimestamp(snapshot.ended_at_unix_ms);
	}
	out << L"\r\nDuration (ms)\t" << snapshot.duration_ms << L"\r\n";
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
		<< L"</code></p><dl><dt>Started UTC</dt><dd>"
		<< isoUtcTimestamp(snapshot.started_at_unix_ms)
		<< L"</dd><dt>Ended UTC</dt><dd>";
	if (snapshot.ended_at_unix_ms != 0) {
		out << isoUtcTimestamp(snapshot.ended_at_unix_ms);
	}
	out << L"</dd><dt>Duration (ms)</dt><dd>" << snapshot.duration_ms
		<< L"</dd></dl><table><thead><tr>";
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

[[nodiscard]] std::wstring serializeCsv(const WinMTRTraceSnapshot& snapshot)
{
	const auto headers = exportHeaders();
	std::wostringstream out;
	out << L"target,session_id,row_kind,ip,started_at_utc,ended_at_utc,duration_ms,";
	for (size_t index = 0; index < headers.size(); ++index) {
		if (index != 0) out << L',';
		out << csvCell(headers[index]);
	}
	out << L"\r\n";
	for (const auto& row : makeRows(snapshot)) {
		out << csvCell(snapshot.target) << L',' << snapshot.session_id << L','
			<< csvCell(row.kind, false) << L',' << csvCell(row.ip) << L','
			<< csvCell(isoUtcTimestamp(snapshot.started_at_unix_ms), false) << L',';
		if (snapshot.ended_at_unix_ms != 0) {
			out << csvCell(isoUtcTimestamp(snapshot.ended_at_unix_ms), false);
		}
		out << L',' << snapshot.duration_ms << L',';
		for (size_t index = 0; index < row.cells.size(); ++index) {
			if (index != 0) out << L',';
			out << csvCell(row.cells[index]);
		}
		out << L"\r\n";
	}
	return out.str();
}

[[nodiscard]] std::wstring nullableNumber(bool available, std::wstring value)
{
	return available ? std::move(value) : L"null";
}

[[nodiscard]] std::wstring serializeJson(const WinMTRTraceSnapshot& snapshot)
{
	std::wostringstream out;
	out << L"{\r\n  \"schema_version\": 1,"
		<< L"\r\n  \"session_id\": " << snapshot.session_id << L','
		<< L"\r\n  \"statistics\": {\"loss\":\"timed_out/completed\","
		<< L"\"stddev\":\"sample_standard_deviation\","
		<< L"\"jitter\":\"ewma_absolute_consecutive_delta_alpha_1_16\"},"
		<< L"\r\n  \"target\": \"" << jsonEscape(snapshot.target) << L"\","
		<< L"\r\n  \"started_at_utc\": \""
		<< isoUtcTimestamp(snapshot.started_at_unix_ms) << L"\","
		<< L"\r\n  \"ended_at_utc\": ";
	if (snapshot.ended_at_unix_ms == 0) out << L"null";
	else out << L'\"' << isoUtcTimestamp(snapshot.ended_at_unix_ms) << L'\"';
	out << L",\r\n  \"duration_ms\": " << snapshot.duration_ms << L','
		<< L"\r\n  \"hops\": [";
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
			<< L"\"cancelled\":" << hop.cancelled << L','
			<< L"\"scheduler_skipped\":" << hop.scheduler_skipped << L','
			<< L"\"cache_skipped\":" << hop.cache_skipped << L','
			<< L"\"late_completions\":" << hop.late_completions << L','
			<< L"\"post_destination_completions\":"
			<< hop.post_destination_completions << L','
			<< L"\"scheduler_late_slots\":" << hop.scheduler_late_slots << L','
			<< L"\"scheduler_lateness_total_ms\":"
			<< hop.scheduler_lateness_total_ms << L','
			<< L"\"scheduler_lateness_max_ms\":" << hop.scheduler_lateness_max_ms << L','
			<< L"\"last_outcome\":\"" << outcomeName(hop.last_outcome) << L"\","
			<< L"\"last_error_code\":" << hop.last_error_code << L','
			<< L"\"best_ms\":" << nullableNumber(hop.returned != 0,
				std::to_wstring(hop.best)) << L','
			<< L"\"average_ms\":" << nullableNumber(hop.returned != 0,
				std::format(L"{:.2f}", hop.getAverageMs())) << L','
			<< L"\"worst_ms\":" << nullableNumber(hop.returned != 0,
				std::to_wstring(hop.worst)) << L','
			<< L"\"last_ms\":" << nullableNumber(hop.returned != 0,
				std::to_wstring(hop.last)) << L','
			<< L"\"jitter_ms\":" << nullableNumber(hop.returned != 0,
				std::format(L"{:.2f}", hop.jitter)) << L','
			<< L"\"recent_jitter_ms\":" << nullableNumber(hop.returned != 0,
				std::format(L"{:.2f}", hop.recent_jitter_ms)) << L','
			<< L"\"stddev_ms\":" << nullableNumber(hop.returned != 0,
				std::format(L"{:.2f}", hop.stddev)) << L','
			<< L"\"country\":\"" << jsonEscape(hop.country) << L"\","
			<< L"\"asn\":\"" << jsonEscape(hop.asn) << L"\","
			<< L"\"isp\":\"" << jsonEscape(hop.isp) << L"\","
			<< L"\"metadata_source\":\"" << jsonEscape(hop.metadata_source) << L"\","
			<< L"\"metadata_failure_reason\":\""
			<< jsonEscape(hop.metadata_failure_reason) << L"\","
			<< L"\"responders\":[";
		for (size_t responderIndex = 0; responderIndex < hop.responders.size(); ++responderIndex) {
			const auto& responder = hop.responders[responderIndex];
			if (responderIndex != 0) out << L',';
			out << L"{\"id\":\"" << std::format(L"{:016x}", responder.stable_id)
				<< L"\","
				<< L"\"hit_count\":" << responder.hit_count << L','
				<< L"\"last_seen_sequence\":" << responder.last_seen_sequence << L','
				<< L"\"best_ms\":" << responder.best_ms << L','
				<< L"\"average_ms\":"
				<< std::format(L"{:.2f}", responder.getAverageMs()) << L','
				<< L"\"worst_ms\":" << responder.worst_ms << L','
				<< L"\"last_ms\":" << responder.last_ms << L','
				<< L"\"jitter_ms\":" << std::format(L"{:.2f}", responder.jitter_ms) << L','
				<< L"\"recent_jitter_ms\":"
				<< std::format(L"{:.2f}", responder.recent_jitter_ms) << L','
				<< L"\"stddev_ms\":" << std::format(L"{:.2f}", responder.stddev_ms) << L','
				<< L"\"host\":\"" << jsonEscape(responder.getName()) << L"\","
				<< L"\"ip\":\"" << jsonEscape(addr_to_string(responder.addr)) << L"\","
				<< L"\"country\":\"" << jsonEscape(responder.country) << L"\","
				<< L"\"asn\":\"" << jsonEscape(responder.asn) << L"\","
				<< L"\"isp\":\"" << jsonEscape(responder.isp) << L"\","
				<< L"\"metadata_source\":\""
				<< jsonEscape(responder.metadata_source) << L"\","
				<< L"\"metadata_failure_reason\":\""
				<< jsonEscape(responder.metadata_failure_reason) << L"\"}";
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

bool writeAll(HANDLE file, const void* data, size_t size, DWORD& error) noexcept
{
	const auto* cursor = static_cast<const std::byte*>(data);
	while (size != 0) {
		const DWORD requested = static_cast<DWORD>(std::min<size_t>(size, MAXDWORD));
		DWORD written = 0;
		if (!WriteFile(file, cursor, requested, &written, nullptr) || written == 0) {
			error = GetLastError();
			if (error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
			return false;
		}
		cursor += written;
		size -= written;
	}
	return true;
}

[[nodiscard]] DWORD writeUtf8File(const CString& path, std::wstring_view contents, bool bom)
{
	CString directory(path);
	const int separator = std::max(directory.ReverseFind(L'\\'), directory.ReverseFind(L'/'));
	if (separator >= 0) directory = directory.Left(separator);
	else directory = L".";
	wchar_t temporaryPath[MAX_PATH]{};
	if (GetTempFileNameW(directory, L"wmt", 0, temporaryPath) == 0) return GetLastError();
	HANDLE file = CreateFileW(temporaryPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		const DWORD error = GetLastError();
		DeleteFileW(temporaryPath);
		return error;
	}
	DWORD error = ERROR_SUCCESS;
	const auto bytes = toUtf8(contents);
	if (bom) {
		constexpr unsigned char marker[]{ 0xef, 0xbb, 0xbf };
		writeAll(file, marker, sizeof(marker), error);
	}
	if (error == ERROR_SUCCESS && !bytes.empty()) {
		writeAll(file, bytes.data(), bytes.size(), error);
	}
	if (error == ERROR_SUCCESS && !FlushFileBuffers(file)) error = GetLastError();
	if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
	if (error == ERROR_SUCCESS && !MoveFileExW(temporaryPath, path,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		error = GetLastError();
	}
	if (error != ERROR_SUCCESS) DeleteFileW(temporaryPath);
	return error;
}

[[nodiscard]] CString systemErrorMessage(DWORD error)
{
	wchar_t* buffer = nullptr;
	const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
	CString message = length == 0 || buffer == nullptr ? L"Unknown error" : buffer;
	if (buffer != nullptr) LocalFree(buffer);
	message.Trim();
	return message;
}

void showExportError(const CString& path, DWORD error)
{
	CString message;
	message.Format(L"%s\r\n\r\n%s\r\n%s (%lu)", localized(IDS_ERROR_EXPORT).GetString(),
		path.GetString(), systemErrorMessage(error).GetString(), error);
	AfxMessageBox(message, MB_OK | MB_ICONERROR);
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
	if (!path.IsEmpty()) {
		if (const DWORD error = writeUtf8File(path, serializeText(wmtrnet->getTraceSnapshot()), true);
			error != ERROR_SUCCESS) showExportError(path, error);
	}
}

void WinMTRDialog::exportHtml()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"html", makeExportFilter(IDS_FILE_TYPE_HTML, L"*.html;*.htm"));
	if (!path.IsEmpty()) {
		if (const DWORD error = writeUtf8File(path, serializeHtml(wmtrnet->getTraceSnapshot(), true), false);
			error != ERROR_SUCCESS) showExportError(path, error);
	}
}

void WinMTRDialog::exportCsv()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"csv", makeExportFilter(IDS_FILE_TYPE_CSV, L"*.csv"));
	if (!path.IsEmpty()) {
		if (const DWORD error = writeUtf8File(path, serializeCsv(wmtrnet->getTraceSnapshot()), true);
			error != ERROR_SUCCESS) showExportError(path, error);
	}
}

void WinMTRDialog::exportJson()
{
	if (!confirmShare()) return;
	const auto path = selectExportPath(this, L"json", makeExportFilter(IDS_FILE_TYPE_JSON, L"*.json"));
	if (!path.IsEmpty()) {
		if (const DWORD error = writeUtf8File(path, serializeJson(wmtrnet->getTraceSnapshot()), false);
			error != ERROR_SUCCESS) showExportError(path, error);
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
