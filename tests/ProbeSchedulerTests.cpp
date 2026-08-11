#include "WinMTRProbeScheduler.h"
#include "WinMTRProbeParameters.h"
#include "WinMTRProviderJson.h"
#include "WinMTRAddressPolicy.h"
#include "WinMTRJson.h"
#include "WinMTRSerialization.h"
#include "WinMTRRoutePolicy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace winmtr::probe;

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

SchedulerConfig standard_config(unsigned hops = 30)
{
	return SchedulerConfig{
		.interval_ms = 1'000,
		.timeout_ms = 3'000,
		.first_ttl = 1,
		.last_ttl = hops,
		.max_inflight = std::max(128u, hops * 3u),
		.max_inflight_per_ttl = 3,
		.max_transport_outstanding = std::max(256u, hops * 4u),
		.max_transport_outstanding_per_ttl = 4,
	};
}

void test_silent_30_hops_keep_one_hertz_cadence()
{
	ProbeScheduler scheduler(standard_config());
	scheduler.start(7, 11, 0);
	std::multimap<MonotonicMilliseconds, ProbeToken> os_completions;
	std::vector<std::uint64_t> maximum_inflight(31);
	std::vector<MonotonicMilliseconds> first_send(31, -1);

	for (MonotonicMilliseconds now = 0; now <= 60'000; ++now) {
		while (!os_completions.empty() && os_completions.begin()->first <= now) {
			const auto token = os_completions.begin()->second;
			os_completions.erase(os_completions.begin());
			require(scheduler.complete(token, CompletionKind::timeout, now)
				== CompletionDisposition::accepted_timeout,
				"silent completion was not accepted as timeout");
		}
		for (const auto& expired : scheduler.expire(now)) {
			(void)expired;
		}
		auto due = scheduler.reserve_due(now);
		require(due.skipped_ttls.empty(), "normal silent cadence hit backpressure");
		for (const auto& slot : due.slots) {
			require(scheduler.mark_issued(slot.token, now), "could not mark slot issued");
			if (first_send[slot.token.ttl] < 0) first_send[slot.token.ttl] = now;
			os_completions.emplace(now + 3'000, slot.token);
		}
		for (unsigned ttl = 1; ttl <= 30; ++ttl) {
			maximum_inflight[ttl] = std::max(maximum_inflight[ttl],
				scheduler.counters(ttl).in_flight);
		}
	}

	for (unsigned ttl = 1; ttl <= 30; ++ttl) {
		const auto& counters = scheduler.counters(ttl);
		require(counters.sent >= 60 && counters.sent <= 61,
			"silent TTL did not maintain approximately 60 sends in 60 seconds");
		require(maximum_inflight[ttl] <= 3,
			"silent TTL exceeded three logical in-flight probes");
		require(counters.received == 0, "silent TTL unexpectedly received a reply");
		require(counters.local_errors == 0, "silent TTL was misclassified as local error");
		const auto expected_offset = static_cast<MonotonicMilliseconds>(ttl - 1u)
			* 1'000 / 30;
		require(first_send[ttl] == expected_offset,
			"first TTL sweep was not uniformly staggered across the interval");
	}
}

void test_slow_reply_is_not_cut_off_by_interval()
{
	ProbeScheduler scheduler(standard_config(1));
	scheduler.start(1, 1, 0);
	ProbeToken first{};
	std::uint64_t maximum_inflight = 0;

	for (MonotonicMilliseconds now = 0; now <= 3'100; ++now) {
		if (now == 2'999) {
			require(scheduler.complete(first, CompletionKind::reply, now)
				== CompletionDisposition::accepted_reply,
				"reply inside the three-second deadline was rejected");
		}
		for (const auto& expired : scheduler.expire(now)) {
			(void)expired;
		}
		auto due = scheduler.reserve_due(now);
		for (const auto& slot : due.slots) {
			require(scheduler.mark_issued(slot.token, now), "could not issue slow-reply probe");
			if (slot.token.sequence == 1) first = slot.token;
		}
		maximum_inflight = std::max(maximum_inflight, scheduler.counters(1).in_flight);
	}

	const auto& counters = scheduler.counters(1);
	require(counters.sent == 4, "one-second send cadence changed under slow reply");
	require(counters.received == 1 && counters.timed_out == 0,
		"slow reply was counted as loss");
	require(maximum_inflight <= 3, "slow-reply test exceeded per-TTL in-flight cap");
}

void test_late_reply_is_discarded_monotonically()
{
	ProbeScheduler scheduler(standard_config(1));
	scheduler.start(2, 3, 0);
	auto due = scheduler.reserve_due(0);
	require(due.slots.size() == 1, "expected first probe slot");
	const auto token = due.slots.front().token;
	require(scheduler.mark_issued(token, 0), "could not issue late-reply probe");
	require(scheduler.expire(3'000).size() == 1, "deadline did not expire probe");
	const auto before = scheduler.counters(1);
	require(scheduler.complete(token, CompletionKind::reply, 3'500)
		== CompletionDisposition::late_discarded,
		"late reply was not discarded");
	const auto& after = scheduler.counters(1);
	require(after.completed == before.completed && after.timed_out == before.timed_out,
		"late reply rewrote completed loss statistics");
	require(after.received == 0 && after.late_completions == 1,
		"late reply diagnostic counters are wrong");
}

void test_local_failure_and_backpressure_are_not_network_loss()
{
	auto config = standard_config(1);
	config.max_inflight = 1;
	config.max_inflight_per_ttl = 1;
	ProbeScheduler scheduler(config);
	scheduler.start(4, 5, 0);

	auto first = scheduler.reserve_due(0);
	require(scheduler.mark_issued(first.slots.front().token, 0), "could not issue first probe");
	auto skipped = scheduler.reserve_due(1'000);
	require(skipped.slots.empty() && skipped.skipped_ttls.size() == 1,
		"in-flight cap did not apply backpressure");
	require(scheduler.complete(first.slots.front().token, CompletionKind::local_error, 1'100)
		== CompletionDisposition::accepted_local_error,
		"local failure was not accepted");

	const auto& counters = scheduler.counters(1);
	require(counters.sent == 1 && counters.completed == 0 && counters.timed_out == 0,
		"local failure polluted network loss denominator");
	require(counters.local_errors == 1 && counters.scheduler_skipped == 1,
		"local failure/backpressure diagnostics are wrong");
	require(std::abs(counters.loss_percent()) < 0.0001,
		"local failure/backpressure was reported as network loss");
}

void test_scheduler_lateness_is_observable()
{
	ProbeScheduler scheduler(standard_config(1));
	scheduler.start(8, 13, 0);
	auto due = scheduler.reserve_due(50);
	require(due.slots.size() == 1, "late scheduler wake did not reserve a probe");
	require(due.slots.front().scheduled_at == 0,
		"scheduler discarded the original due timestamp");
	const auto& counters = scheduler.counters(1);
	require(counters.scheduler_late_slots == 1
		&& counters.scheduler_lateness_total_ms == 50
		&& counters.scheduler_lateness_max_ms == 50,
		"scheduler lateness diagnostics are wrong");
}

void test_restart_epoch_race_10_000_times()
{
	ProbeScheduler scheduler(standard_config(2));
	scheduler.start(99, 1, 0);
	for (std::uint64_t epoch = 2; epoch <= 10'001; ++epoch) {
		auto due = scheduler.reserve_due(static_cast<MonotonicMilliseconds>(epoch) * 1'000);
		std::vector<ProbeToken> old_tokens;
		for (const auto& slot : due.slots) {
			require(scheduler.mark_issued(slot.token,
				static_cast<MonotonicMilliseconds>(epoch) * 1'000),
				"stress test could not issue probe");
			old_tokens.push_back(slot.token);
		}
		scheduler.restart(epoch, static_cast<MonotonicMilliseconds>(epoch) * 1'000);
		for (const auto& token : old_tokens) {
			require(scheduler.complete(token, CompletionKind::reply,
				static_cast<MonotonicMilliseconds>(epoch) * 1'000 + 1)
				== CompletionDisposition::ignored_epoch,
				"old-epoch completion escaped restart guard");
		}
		require(scheduler.logical_inflight() == 0,
			"restart left a logical probe in flight");
	}
	scheduler.stop();
	require(scheduler.logical_inflight() == 0 && scheduler.transport_outstanding() == 0,
		"stress stop did not drain scheduler state");
}

void test_start_stop_late_completion_race_10_000_times()
{
	ProbeScheduler scheduler(standard_config(1));
	for (std::uint64_t iteration = 1; iteration <= 10'000; ++iteration) {
		const auto now = static_cast<MonotonicMilliseconds>(iteration) * 10;
		scheduler.start(iteration, iteration, now);
		auto due = scheduler.reserve_due(now);
		require(due.slots.size() == 1, "lifecycle stress did not reserve initial slot");
		const auto token = due.slots.front().token;
		require(scheduler.mark_issued(token, now), "lifecycle stress could not issue probe");
		scheduler.stop();
		require(scheduler.logical_inflight() == 0,
			"stop retained a logical in-flight probe");
		require(scheduler.complete(token, CompletionKind::reply, now + 1)
			== CompletionDisposition::ignored_epoch,
			"completion after stop escaped generation guard");
		require(scheduler.transport_outstanding() == 0,
			"completion after stop leaked transport state");
	}
}

void test_json_string_parser_boundaries()
{
	const auto escapedKey = winmtr::json::get_string(
		R"({"\u0069p":"203.0.113.7"})", "ip");
	require(escapedKey && *escapedKey == "203.0.113.7",
		"escaped JSON object key was not decoded");
	const auto surrogatePair = winmtr::json::get_string(
		R"({"city":"\uD83D\uDE00"})", "city");
	require(surrogatePair && *surrogatePair == "\xF0\x9F\x98\x80",
		"JSON surrogate pair was not combined into UTF-8");
	require(!winmtr::json::get_string(R"({"ip":"a","ip":"b"})", "ip"),
		"duplicate JSON key was accepted");
	require(!winmtr::json::get_string(R"({"ip":null})", "ip"),
		"JSON null was treated as a string");
	require(!winmtr::json::get_string(R"({"city":"\uDE00"})", "city"),
		"unpaired low surrogate was accepted");

	std::string deep = R"({"value":)";
	deep.append(65, '[');
	deep += R"("x")";
	deep.append(65, ']');
	deep.push_back('}');
	require(!winmtr::json::get_string(deep, "value"),
		"over-deep JSON input was accepted");
	std::string oversized(1024u * 1024u + 1u, ' ');
	require(!winmtr::json::get_string(oversized, "value"),
		"oversized JSON input was accepted");
	std::string oversizedField = R"({"value":")";
	oversizedField.append(4'097, 'x');
	oversizedField += R"("})";
	require(!winmtr::json::get_string(oversizedField, "value"),
		"oversized JSON string field was accepted");
}

constexpr std::uint32_t ipv4(unsigned a, unsigned b, unsigned c, unsigned d)
{
	return (a << 24u) | (b << 16u) | (c << 8u) | d;
}

void test_special_use_address_policy()
{
	using winmtr::address_policy::is_public_ipv4;
	using winmtr::address_policy::is_public_ipv6;
	require(is_public_ipv4(ipv4(8, 8, 8, 8)), "public IPv4 was rejected");
	for (const auto address : {
		ipv4(0, 1, 2, 3), ipv4(10, 0, 0, 1), ipv4(100, 64, 0, 1),
		ipv4(127, 0, 0, 1), ipv4(169, 254, 1, 1), ipv4(172, 31, 255, 255),
		ipv4(192, 0, 2, 1), ipv4(192, 168, 1, 1), ipv4(198, 18, 0, 1),
		ipv4(198, 51, 100, 1), ipv4(203, 0, 113, 1), ipv4(224, 0, 0, 1) }) {
		require(!is_public_ipv4(address), "special-use IPv4 was accepted");
	}

	std::array<std::uint8_t, 16> publicIpv6{ 0x26, 0x06, 0x47, 0x00 };
	require(is_public_ipv6(publicIpv6), "public IPv6 was rejected");
	for (const auto& address : std::array{
		std::array<std::uint8_t, 16>{ 0xfc },
		std::array<std::uint8_t, 16>{ 0xfe, 0x80 },
		std::array<std::uint8_t, 16>{ 0x20, 0x01, 0x00, 0x02 },
		std::array<std::uint8_t, 16>{ 0x20, 0x01, 0x00, 0x10 },
		std::array<std::uint8_t, 16>{ 0x20, 0x01, 0x00, 0x20 },
		std::array<std::uint8_t, 16>{ 0x20, 0x01, 0x0d, 0xb8 },
		std::array<std::uint8_t, 16>{ 0x3f, 0xff } }) {
		require(!is_public_ipv6(address), "special-use IPv6 was accepted");
	}
}

void test_serialization_security_and_unicode()
{
	using winmtr::serialization::csvCell;
	using winmtr::serialization::jsonEscape;
	require(csvCell(L"=SUM(A1:A2)") == L"'=SUM(A1:A2)",
		"CSV formula prefix was not neutralized");
	require(csvCell(L"+cmd") == L"'+cmd" && csvCell(L"-1") == L"'-1"
		&& csvCell(L"@name") == L"'@name",
		"CSV formula protection did not cover all dangerous prefixes");
	require(csvCell(L"a,\"b\"") == L"\"a,\"\"b\"\"\"",
		"CSV quoting did not preserve comma and quotes");
	require(jsonEscape(L"台灣🌐") == L"台灣🌐",
		"valid non-BMP JSON text was changed");
	require(jsonEscape(L"line\nquote\"") == L"line\\nquote\\\"",
		"JSON controls and quotes were not escaped");
	const std::wstring unpairedHigh(1, static_cast<wchar_t>(0xd800));
	const std::wstring unpairedLow(1, static_cast<wchar_t>(0xdc00));
	require(jsonEscape(unpairedHigh) == L"\\ufffd"
		&& jsonEscape(unpairedLow) == L"\\ufffd",
		"unpaired UTF-16 surrogate was not replaced deterministically");
}

void test_global_rate_limit_is_fair()
{
	auto config = standard_config(4);
	config.interval_ms = 100;
	config.timeout_ms = 1'000;
	config.max_global_pps = 10;
	ProbeScheduler scheduler(config);
	scheduler.start(1, 1, 0);
	std::array<std::uint64_t, 4> sent{};
	for (MonotonicMilliseconds now = 0; now < 1'000; ++now) {
		auto due = scheduler.reserve_due(now);
		for (const auto& slot : due.slots) {
			require(scheduler.mark_issued(slot.token, now), "rate-limited slot was not issued");
			++sent[slot.token.ttl - 1];
			require(scheduler.complete(slot.token, CompletionKind::reply, now)
				== CompletionDisposition::accepted_reply,
				"rate-limited completion was rejected");
		}
	}
	const auto total = std::accumulate(sent.begin(), sent.end(), std::uint64_t{ 0 });
	require(total == 10, "global 10 pps cap did not produce exactly ten slots in one second");
	require(std::ranges::all_of(sent, [](std::uint64_t value) { return value >= 2; }),
		"oldest-due scheduling starved a TTL under the global cap");
}

void test_grace_cancellation_is_not_loss()
{
	auto config = standard_config(1);
	ProbeScheduler scheduler(config);
	scheduler.start(7, 9, 0);
	auto due = scheduler.reserve_due(0);
	require(due.slots.size() == 1, "grace test did not reserve a probe");
	const auto token = due.slots.front().token;
	require(scheduler.mark_issued(token, 0), "grace test did not issue a probe");
	scheduler.begin_drain();
	require(scheduler.reserve_due(1'000).slots.empty(), "draining scheduler emitted a new probe");
	const auto cancelled = scheduler.cancel_pending();
	require(cancelled.size() == 1 && cancelled.front() == token,
		"grace expiry did not return the pending token");
	const auto& counters = scheduler.counters(1);
	require(counters.cancelled == 1 && counters.in_flight == 0
		&& counters.completed == 0 && counters.timed_out == 0,
		"cancelled probe changed network-loss counters");
	require(scheduler.complete(token, CompletionKind::reply, 2'000)
		== CompletionDisposition::cancelled,
		"transport completion after cancellation was not cleanup-only");
	require(scheduler.transport_outstanding() == 0,
		"cancelled transport resource was not retired");
}

void test_deterministic_scheduler_resource_budget()
{
	auto config = standard_config(64);
	config.interval_ms = 100;
	config.timeout_ms = 10'000;
	config.max_inflight = 1'024;
	config.max_inflight_per_ttl = 100;
	config.max_transport_outstanding = 1'024;
	config.max_transport_outstanding_per_ttl = 100;
	config.max_global_pps = 100;
	ProbeScheduler scheduler(config);
	scheduler.start(77, 91, 0);
	std::array<std::uint64_t, 64> sent{};
	std::uint64_t maximumInflight = 0;
	for (MonotonicMilliseconds now = 0; now < 10'000; ++now) {
		for (const auto& expired : scheduler.expire(now)) (void)expired;
		for (const auto& slot : scheduler.reserve_due(now).slots) {
			require(scheduler.mark_issued(slot.token, now), "budget probe was not issued");
			++sent[slot.token.ttl - 1];
		}
		maximumInflight = std::max(maximumInflight, scheduler.logical_inflight());
	}
	const auto total = std::accumulate(sent.begin(), sent.end(), std::uint64_t{ 0 });
	require(total == 1'000, "100 pps budget did not cap ten seconds at 1,000 probes");
	require(maximumInflight <= 1'000, "deterministic workload exceeded bounded in-flight state");
	require(std::ranges::all_of(sent, [](std::uint64_t value) { return value >= 15; }),
		"resource budget scheduler starved a TTL");
}

void test_route_policy_scripted_changes()
{
	using winmtr::route::RoutePolicy;
	using winmtr::route::RoutePolicyConfig;
	RoutePolicy policy(RoutePolicyConfig{
		.start_ttl = 1,
		.minimum_ttl = 0,
		.max_hops = 30,
		.unknown_host_limit = 12,
		.exploration_period = 10,
		.exploration_frontier_ttls = 5,
		.shrink_confirmations = 2,
	});

	policy.note_reply(8, true);
	require(policy.complete_cycle(1) == 8u && policy.stable_ceiling() == 8u,
		"initial destination did not establish the route ceiling");
	require(policy.complete_cycle(10) == 13u && policy.exploring(),
		"periodic frontier did not extend five TTLs");
	policy.note_reply(12, true);
	require(policy.complete_cycle(11) == 12u && policy.stable_ceiling() == 12u,
		"longer route was not adopted after frontier discovery");

	require(policy.complete_cycle(20) == 17u, "second frontier was not scheduled");
	policy.note_reply(6, true);
	require(policy.complete_cycle(21) == 12u && policy.stable_ceiling() == 12u,
		"route shrank without the required hysteresis confirmation");
	require(policy.complete_cycle(30) == 17u, "shrink confirmation frontier was not scheduled");
	policy.note_reply(6, true);
	require(policy.complete_cycle(31) == 6u && policy.stable_ceiling() == 6u,
		"confirmed shorter route was not adopted");

	policy.reset();
	policy.note_reply(8, false);
	require(policy.complete_cycle(1) == 20u,
		"silent destination did not retain the configured unknown tail");

	RoutePolicy minimumPolicy(RoutePolicyConfig{
		.start_ttl = 1, .minimum_ttl = 15, .max_hops = 30,
		.unknown_host_limit = 5, .exploration_period = 10,
		.exploration_frontier_ttls = 5, .shrink_confirmations = 2,
	});
	minimumPolicy.note_reply(6, true);
	require(minimumPolicy.complete_cycle(1) == 15u,
		"minimum TTL was not enforced after an early destination");
}

void test_probe_packet_parameters()
{
	using winmtr::probe_parameters::make_payload;
	using winmtr::probe_parameters::make_wire_options;
	std::uint64_t fixedState = 123;
	require(make_payload(0, 32, fixedState).empty(), "zero-byte payload was not supported");
	const auto maximum = make_payload(4'096, 0xab, fixedState);
	require(maximum.size() == 4'096
		&& std::ranges::all_of(maximum, [](std::byte value) { return value == std::byte{ 0xab }; }),
		"fixed maximum-size payload did not preserve its pattern");

	std::uint64_t firstState = 0x123456789abcdef0ull;
	std::uint64_t secondState = firstState;
	const auto randomFirst = make_payload(64, -1, firstState);
	const auto randomSecond = make_payload(64, -1, secondState);
	require(randomFirst == randomSecond && firstState == secondState,
		"random payload was not reproducible from its seed");
	require(std::ranges::any_of(randomFirst,
		[head = randomFirst.front()](std::byte value) { return value != head; }),
		"random payload did not vary its bytes");

	const auto ipv4 = make_wire_options(64, 255, true, true);
	require(ipv4.ttl == 64 && ipv4.tos == 255 && ipv4.dont_fragment,
		"IPv4 TTL/ToS/DF parameters changed");
	const auto ipv6 = make_wire_options(64, 255, true, false);
	require(ipv6.ttl == 64 && ipv6.tos == 255 && !ipv6.dont_fragment,
		"IPv4-only DF flag leaked into IPv6");
}

void test_provider_json_fields()
{
	const auto ipinfo = winmtr::provider_json::parse_ipinfo(
		R"({"ip":"2001:db8::1","hostname":"node.\u4f8b\u5b50","city":"Taipei","region":"Taiwan","country":"TW","org":"AS64500 Example, Inc."})");
	require(ipinfo.address && *ipinfo.address == "2001:db8::1",
		"ipinfo address was not parsed");
	require(ipinfo.hostname && *ipinfo.hostname == "node.\xE4\xBE\x8B\xE5\xAD\x90",
		"ipinfo Unicode hostname was not decoded");
	require(ipinfo.country_code && *ipinfo.country_code == "TW"
		&& ipinfo.organization && *ipinfo.organization == "AS64500 Example, Inc.",
		"ipinfo country/organization fields changed");

	const auto ipapi = winmtr::provider_json::parse_ipapi(
		R"({"ip":"203.0.113.1","city":"Taipei","region":"Taiwan","country_name":"Taiwan","country_code":"TW","asn":"AS64500","org":"Example"})");
	require(ipapi.country && *ipapi.country == "Taiwan"
		&& ipapi.asn && *ipapi.asn == "AS64500",
		"ipapi country/ASN fields changed");

	const auto duplicate = winmtr::provider_json::parse_ipinfo(
		R"({"ip":"203.0.113.1","ip":"198.51.100.2","city":"Taipei"})");
	require(!duplicate.address && !duplicate.city,
		"provider object with duplicate keys was partially accepted");
	const auto wrongTypes = winmtr::provider_json::parse_ipapi(
		R"({"ip":null,"city":123,"org":false})");
	require(!wrongTypes.address && !wrongTypes.city && !wrongTypes.organization,
		"non-string provider fields were accepted");
}

void fuzz_json_parser_offline()
{
	std::uint64_t state = 0x243f6a8885a308d3ull;
	for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		const std::size_t length = static_cast<std::size_t>(state % 2048u);
		std::string input(length, '\0');
		for (char& value : input) {
			state = state * 6364136223846793005ull + 1442695040888963407ull;
			value = static_cast<char>(state >> 56);
		}
		(void)winmtr::json::get_string(input, "ip");
		(void)winmtr::provider_json::parse_ipinfo(input);
		(void)winmtr::provider_json::parse_ipapi(input);
	}
}

} // namespace

int main()
{
	try {
		test_silent_30_hops_keep_one_hertz_cadence();
		test_slow_reply_is_not_cut_off_by_interval();
		test_late_reply_is_discarded_monotonically();
		test_local_failure_and_backpressure_are_not_network_loss();
		test_scheduler_lateness_is_observable();
		test_restart_epoch_race_10_000_times();
		test_start_stop_late_completion_race_10_000_times();
		test_json_string_parser_boundaries();
		test_special_use_address_policy();
		test_serialization_security_and_unicode();
		test_global_rate_limit_is_fair();
		test_grace_cancellation_is_not_loss();
		test_deterministic_scheduler_resource_budget();
		test_route_policy_scripted_changes();
		test_probe_packet_parameters();
		test_provider_json_fields();
		fuzz_json_parser_offline();
		std::cout << "All probe scheduler tests passed.\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << "Probe scheduler test failure: " << error.what() << '\n';
		return 1;
	}
}
