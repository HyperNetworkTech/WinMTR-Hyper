#include "WinMTRProbeScheduler.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
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

} // namespace

int main()
{
	try {
		test_silent_30_hops_keep_one_hertz_cadence();
		test_slow_reply_is_not_cut_off_by_interval();
		test_late_reply_is_discarded_monotonically();
		test_local_failure_and_backpressure_are_not_network_loss();
		test_restart_epoch_race_10_000_times();
		test_start_stop_late_completion_race_10_000_times();
		std::cout << "All probe scheduler tests passed.\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << "Probe scheduler test failure: " << error.what() << '\n';
		return 1;
	}
}
