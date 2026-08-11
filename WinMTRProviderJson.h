#pragma once

#include "WinMTRJson.h"

#include <optional>
#include <string>
#include <string_view>

namespace winmtr::provider_json {

struct ProviderFields final {
	std::optional<std::string> address;
	std::optional<std::string> hostname;
	std::optional<std::string> city;
	std::optional<std::string> region;
	std::optional<std::string> country;
	std::optional<std::string> country_code;
	std::optional<std::string> asn;
	std::optional<std::string> organization;
};

[[nodiscard]] inline ProviderFields parse_ipinfo(std::string_view json)
{
	return ProviderFields{
		.address = json::get_string(json, "ip"),
		.hostname = json::get_string(json, "hostname"),
		.city = json::get_string(json, "city"),
		.region = json::get_string(json, "region"),
		.country_code = json::get_string(json, "country"),
		.organization = json::get_string(json, "org"),
	};
}

[[nodiscard]] inline ProviderFields parse_ipapi(std::string_view json)
{
	return ProviderFields{
		.address = json::get_string(json, "ip"),
		.city = json::get_string(json, "city"),
		.region = json::get_string(json, "region"),
		.country = json::get_string(json, "country_name"),
		.country_code = json::get_string(json, "country_code"),
		.asn = json::get_string(json, "asn"),
		.organization = json::get_string(json, "org"),
	};
}

} // namespace winmtr::provider_json
