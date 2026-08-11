#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cwchar>

namespace winmtr::cli_values {

[[nodiscard]] inline bool parse_integer(const wchar_t* text, long& value) noexcept
{
	if (text == nullptr) return false;
	errno = 0;
	wchar_t* end = nullptr;
	const auto parsed = std::wcstol(text, &end, 10);
	if (text == end || end == nullptr || *end != L'\0' || errno == ERANGE) return false;
	value = parsed;
	return true;
}

[[nodiscard]] inline bool parse_ranged_integer(const wchar_t* text,
	long minimum, long maximum, long& value) noexcept
{
	long parsed = 0;
	if (minimum > maximum || !parse_integer(text, parsed)
		|| parsed < minimum || parsed > maximum) return false;
	value = parsed;
	return true;
}

[[nodiscard]] inline bool parse_floating_point(const wchar_t* text,
	double& value) noexcept
{
	if (text == nullptr) return false;
	errno = 0;
	wchar_t* end = nullptr;
	const auto parsed = std::wcstod(text, &end);
	if (text == end || end == nullptr || *end != L'\0' || errno == ERANGE
		|| !std::isfinite(parsed)) return false;
	value = parsed;
	return true;
}

} // namespace winmtr::cli_values
