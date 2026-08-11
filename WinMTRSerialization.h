#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace winmtr::serialization {

[[nodiscard]] inline std::wstring csvCell(std::wstring_view value,
	bool protectFormula = true)
{
	std::wstring protectedValue;
	if (protectFormula && !value.empty() && (value.front() == L'=' || value.front() == L'+'
		|| value.front() == L'-' || value.front() == L'@')) {
		protectedValue.reserve(value.size() + 1);
		protectedValue.push_back(L'\'');
		protectedValue.append(value);
		value = protectedValue;
	}
	if (value.find_first_of(L",\"\r\n") == std::wstring_view::npos) return std::wstring(value);
	std::wstring result = L"\"";
	for (const wchar_t ch : value) {
		if (ch == L'\"') result += L"\"\"";
		else result.push_back(ch);
	}
	result.push_back(L'\"');
	return result;
}

[[nodiscard]] inline std::wstring jsonEscape(std::wstring_view value)
{
	std::wstring result;
	for (std::size_t index = 0; index < value.size(); ++index) {
		const wchar_t ch = value[index];
		if (ch >= 0xd800 && ch <= 0xdbff) {
			if (index + 1 < value.size() && value[index + 1] >= 0xdc00
				&& value[index + 1] <= 0xdfff) {
				result.push_back(ch);
				result.push_back(value[++index]);
			}
			else result += L"\\ufffd";
			continue;
		}
		if (ch >= 0xdc00 && ch <= 0xdfff) {
			result += L"\\ufffd";
			continue;
		}
		switch (ch) {
		case L'\"': result += L"\\\""; break;
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

} // namespace winmtr::serialization
