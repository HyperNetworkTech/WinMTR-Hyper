#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace winmtr::probe {

using MonotonicMilliseconds = std::int64_t;

struct SchedulerConfig final {
	MonotonicMilliseconds interval_ms = 1'000;
	MonotonicMilliseconds timeout_ms = 3'000;
	unsigned first_ttl = 1;
	unsigned last_ttl = 30;
	unsigned max_inflight = 128;
	unsigned max_inflight_per_ttl = 3;
	unsigned max_transport_outstanding = 256;
	unsigned max_transport_outstanding_per_ttl = 4;
	std::uint64_t probes_per_ttl = 0; // Zero means unlimited.
};

struct ProbeToken final {
	std::uint64_t session = 0;
	std::uint64_t epoch = 0;
	std::uint64_t sequence = 0;
	unsigned ttl = 0;

	friend bool operator==(const ProbeToken&, const ProbeToken&) = default;
};

struct ProbeSlot final {
	ProbeToken token;
	MonotonicMilliseconds scheduled_at = 0;
};

struct DueBatch final {
	std::vector<ProbeSlot> slots;
	std::vector<unsigned> skipped_ttls;
};

struct ProbeCounters final {
	std::uint64_t sent = 0;
	std::uint64_t completed = 0;
	std::uint64_t received = 0;
	std::uint64_t timed_out = 0;
	std::uint64_t in_flight = 0;
	std::uint64_t local_errors = 0;
	std::uint64_t scheduler_skipped = 0;
	std::uint64_t cache_skipped = 0;
	std::uint64_t late_completions = 0;
	std::uint64_t transport_outstanding = 0;
	std::uint64_t scheduler_late_slots = 0;
	std::uint64_t scheduler_lateness_total_ms = 0;
	std::uint64_t scheduler_lateness_max_ms = 0;

	[[nodiscard]] double loss_percent() const noexcept
	{
		return completed == 0
			? 0.0
			: 100.0 * static_cast<double>(timed_out) / static_cast<double>(completed);
	}
};

enum class CompletionKind {
	reply,
	timeout,
	local_error
};

enum class CompletionDisposition {
	accepted_reply,
	accepted_timeout,
	accepted_local_error,
	late_discarded,
	late_discarded_after_timeout,
	ignored_epoch,
	unknown_token
};

class ProbeScheduler final {
public:
	explicit ProbeScheduler(SchedulerConfig config)
		: config_(normalize(config)), ttl_(config_.last_ttl + 1u)
	{
	}

	void start(std::uint64_t session, std::uint64_t epoch,
		MonotonicMilliseconds now)
	{
		session_ = session;
		sequence_ = 0;
		active_ = true;
		restart(epoch, now);
	}

	void restart(std::uint64_t epoch, MonotonicMilliseconds now)
	{
		retire_logical_state_for_epoch_change();
		epoch_ = epoch;
		active_ = true;
		active_last_ttl_ = config_.last_ttl;
		for (unsigned ttl = config_.first_ttl; ttl <= config_.last_ttl; ++ttl) {
			auto& state = ttl_[ttl];
			const auto still_outstanding = state.counters.transport_outstanding;
			state.counters = {};
			state.counters.transport_outstanding = still_outstanding;
			state.reserved = 0;
		}
		initialize_send_deadlines(now);
	}

	void stop() noexcept
	{
		active_ = false;
		retire_logical_state_for_epoch_change();
	}

	void set_last_ttl(unsigned last_ttl, MonotonicMilliseconds now) noexcept
	{
		const auto next_last = std::clamp(last_ttl, config_.first_ttl, config_.last_ttl);
		if (next_last == active_last_ttl_) return;
		if (next_last > active_last_ttl_) {
			const auto added = static_cast<MonotonicMilliseconds>(next_last - active_last_ttl_);
			for (unsigned ttl = active_last_ttl_ + 1u; ttl <= next_last; ++ttl) {
				const auto offset = static_cast<MonotonicMilliseconds>(ttl - active_last_ttl_ - 1u)
					* config_.interval_ms / std::max<MonotonicMilliseconds>(1, added);
				ttl_[ttl].next_send_at = now + offset;
			}
		}
		active_last_ttl_ = next_last;
	}

	[[nodiscard]] DueBatch reserve_due(MonotonicMilliseconds now)
	{
		DueBatch batch;
		if (!active_) return batch;

		for (unsigned ttl = config_.first_ttl; ttl <= active_last_ttl_; ++ttl) {
			auto& state = ttl_[ttl];
			if (now < state.next_send_at || quota_reached(state)) continue;

			const auto scheduled_at = state.next_send_at;
			const auto lateness = now > scheduled_at
				? static_cast<std::uint64_t>(now - scheduled_at)
				: 0u;
			if (lateness != 0) {
				++state.counters.scheduler_late_slots;
				state.counters.scheduler_lateness_total_ms += lateness;
				state.counters.scheduler_lateness_max_ms = std::max(
					state.counters.scheduler_lateness_max_ms, lateness);
			}
			do {
				state.next_send_at += config_.interval_ms;
			} while (state.next_send_at <= now);

			const bool logical_full = state.counters.in_flight + state.reserved
				>= config_.max_inflight_per_ttl
				|| logical_inflight_ + reserved_ >= config_.max_inflight;
			const bool transport_full = state.counters.transport_outstanding + state.reserved
				>= config_.max_transport_outstanding_per_ttl
				|| transport_outstanding_ + reserved_ >= config_.max_transport_outstanding;
			if (logical_full || transport_full) {
				++state.counters.scheduler_skipped;
				batch.skipped_ttls.push_back(ttl);
				continue;
			}

			ProbeToken token{
				.session = session_,
				.epoch = epoch_,
				.sequence = ++sequence_,
				.ttl = ttl,
			};
			records_.emplace(token.sequence, Record{
				.token = token,
				.state = RecordState::reserved,
			});
			++state.reserved;
			++reserved_;
			batch.slots.push_back(ProbeSlot{ token, scheduled_at });
		}
		return batch;
	}

	[[nodiscard]] bool mark_issued(const ProbeToken& token,
		MonotonicMilliseconds actual_send_at)
	{
		auto found = matching_record(token);
		if (found == records_.end() || found->second.state != RecordState::reserved) {
			return false;
		}
		auto& state = ttl_[token.ttl];
		--state.reserved;
		--reserved_;
		++state.counters.sent;
		++state.counters.in_flight;
		++state.counters.transport_outstanding;
		++logical_inflight_;
		++transport_outstanding_;
		found->second.state = RecordState::issued;
		found->second.deadline = actual_send_at + config_.timeout_ms;
		return true;
	}

	[[nodiscard]] bool mark_issue_failed(const ProbeToken& token)
	{
		auto found = matching_record(token);
		if (found == records_.end() || found->second.state != RecordState::reserved) {
			return false;
		}
		auto& state = ttl_[token.ttl];
		--state.reserved;
		--reserved_;
		++state.counters.local_errors;
		records_.erase(found);
		return true;
	}

	[[nodiscard]] bool mark_cached(const ProbeToken& token)
	{
		auto found = matching_record(token);
		if (found == records_.end() || found->second.state != RecordState::reserved) {
			return false;
		}
		auto& state = ttl_[token.ttl];
		--state.reserved;
		--reserved_;
		++state.counters.cache_skipped;
		records_.erase(found);
		return true;
	}

	[[nodiscard]] std::vector<ProbeToken> expire(MonotonicMilliseconds now)
	{
		std::vector<ProbeToken> expired;
		for (auto& [sequence, record] : records_) {
			(void)sequence;
			if (record.state != RecordState::issued || record.deadline > now) continue;
			auto& counters = ttl_[record.token.ttl].counters;
			++counters.completed;
			++counters.timed_out;
			--counters.in_flight;
			--logical_inflight_;
			record.state = RecordState::timed_out;
			expired.push_back(record.token);
		}
		return expired;
	}

	[[nodiscard]] CompletionDisposition complete(const ProbeToken& token,
		CompletionKind kind, MonotonicMilliseconds completed_at)
	{
		auto found = matching_record(token);
		if (found == records_.end()) return CompletionDisposition::unknown_token;
		auto& record = found->second;
		auto& counters = ttl_[token.ttl].counters;

		if (record.state == RecordState::ignored_epoch) {
			retire_transport(counters);
			records_.erase(found);
			return CompletionDisposition::ignored_epoch;
		}
		if (record.state == RecordState::timed_out) {
			++counters.late_completions;
			retire_transport(counters);
			records_.erase(found);
			return CompletionDisposition::late_discarded;
		}
		if (record.state != RecordState::issued) {
			return CompletionDisposition::unknown_token;
		}

		if (completed_at > record.deadline) {
			++counters.completed;
			++counters.timed_out;
			++counters.late_completions;
			--counters.in_flight;
			--logical_inflight_;
			retire_transport(counters);
			records_.erase(found);
			return CompletionDisposition::late_discarded_after_timeout;
		}

		--counters.in_flight;
		--logical_inflight_;
		CompletionDisposition disposition{};
		switch (kind) {
		case CompletionKind::reply:
			++counters.completed;
			++counters.received;
			disposition = CompletionDisposition::accepted_reply;
			break;
		case CompletionKind::timeout:
			++counters.completed;
			++counters.timed_out;
			disposition = CompletionDisposition::accepted_timeout;
			break;
		case CompletionKind::local_error:
			++counters.local_errors;
			disposition = CompletionDisposition::accepted_local_error;
			break;
		}
		retire_transport(counters);
		records_.erase(found);
		return disposition;
	}

	[[nodiscard]] const ProbeCounters& counters(unsigned ttl) const noexcept
	{
		static constexpr ProbeCounters empty{};
		return ttl < ttl_.size() ? ttl_[ttl].counters : empty;
	}

	[[nodiscard]] std::uint64_t logical_inflight() const noexcept
	{
		return logical_inflight_;
	}

	[[nodiscard]] std::uint64_t transport_outstanding() const noexcept
	{
		return transport_outstanding_;
	}

	[[nodiscard]] bool quotas_reached() const noexcept
	{
		if (config_.probes_per_ttl == 0) return false;
		for (unsigned ttl = config_.first_ttl; ttl <= active_last_ttl_; ++ttl) {
			if (!quota_reached(ttl_[ttl])) return false;
		}
		return true;
	}

	[[nodiscard]] std::optional<MonotonicMilliseconds> next_wake_at() const noexcept
	{
		std::optional<MonotonicMilliseconds> result;
		if (active_) {
			for (unsigned ttl = config_.first_ttl; ttl <= active_last_ttl_; ++ttl) {
				if (!quota_reached(ttl_[ttl])) minimize(result, ttl_[ttl].next_send_at);
			}
		}
		for (const auto& [sequence, record] : records_) {
			(void)sequence;
			if (record.state == RecordState::issued) minimize(result, record.deadline);
		}
		return result;
	}

	[[nodiscard]] const SchedulerConfig& config() const noexcept { return config_; }
	[[nodiscard]] unsigned active_last_ttl() const noexcept { return active_last_ttl_; }

private:
	enum class RecordState { reserved, issued, timed_out, ignored_epoch };
	struct Record final {
		ProbeToken token;
		RecordState state = RecordState::reserved;
		MonotonicMilliseconds deadline = 0;
	};
	struct TtlState final {
		ProbeCounters counters;
		MonotonicMilliseconds next_send_at = 0;
		std::uint64_t reserved = 0;
	};

	using RecordMap = std::unordered_map<std::uint64_t, Record>;

	[[nodiscard]] static SchedulerConfig normalize(SchedulerConfig value) noexcept
	{
		value.interval_ms = std::max<MonotonicMilliseconds>(1, value.interval_ms);
		value.timeout_ms = std::max<MonotonicMilliseconds>(1, value.timeout_ms);
		value.first_ttl = std::max(1u, value.first_ttl);
		value.last_ttl = std::max(value.first_ttl, value.last_ttl);
		value.max_inflight_per_ttl = std::max(1u, value.max_inflight_per_ttl);
		value.max_inflight = std::max(value.max_inflight_per_ttl, value.max_inflight);
		value.max_transport_outstanding_per_ttl = std::max(
			value.max_inflight_per_ttl, value.max_transport_outstanding_per_ttl);
		value.max_transport_outstanding = std::max(
			value.max_inflight, value.max_transport_outstanding);
		return value;
	}

	void initialize_send_deadlines(MonotonicMilliseconds now) noexcept
	{
		const auto count = static_cast<MonotonicMilliseconds>(
			active_last_ttl_ - config_.first_ttl + 1u);
		for (unsigned ttl = config_.first_ttl; ttl <= active_last_ttl_; ++ttl) {
			const auto offset = static_cast<MonotonicMilliseconds>(ttl - config_.first_ttl)
				* config_.interval_ms / count;
			ttl_[ttl].next_send_at = now + offset;
		}
	}

	void retire_logical_state_for_epoch_change() noexcept
	{
		for (auto iterator = records_.begin(); iterator != records_.end();) {
			auto& record = iterator->second;
			auto& state = ttl_[record.token.ttl];
			if (record.state == RecordState::reserved) {
				--state.reserved;
				--reserved_;
				iterator = records_.erase(iterator);
				continue;
			}
			if (record.state == RecordState::issued) {
				--state.counters.in_flight;
				--logical_inflight_;
			}
			record.state = RecordState::ignored_epoch;
			++iterator;
		}
	}

	[[nodiscard]] bool quota_reached(const TtlState& state) const noexcept
	{
		return config_.probes_per_ttl != 0
			&& state.counters.sent + state.counters.local_errors
				+ state.counters.cache_skipped + state.reserved
				>= config_.probes_per_ttl;
	}

	[[nodiscard]] RecordMap::iterator matching_record(const ProbeToken& token)
	{
		auto found = records_.find(token.sequence);
		if (found == records_.end() || found->second.token != token) return records_.end();
		return found;
	}

	void retire_transport(ProbeCounters& counters) noexcept
	{
		if (counters.transport_outstanding != 0) --counters.transport_outstanding;
		if (transport_outstanding_ != 0) --transport_outstanding_;
	}

	static void minimize(std::optional<MonotonicMilliseconds>& current,
		MonotonicMilliseconds candidate) noexcept
	{
		if (!current || candidate < *current) current = candidate;
	}

	SchedulerConfig config_;
	std::vector<TtlState> ttl_;
	RecordMap records_;
	std::uint64_t session_ = 0;
	std::uint64_t epoch_ = 0;
	std::uint64_t sequence_ = 0;
	std::uint64_t logical_inflight_ = 0;
	std::uint64_t transport_outstanding_ = 0;
	std::uint64_t reserved_ = 0;
	unsigned active_last_ttl_ = config_.last_ttl;
	bool active_ = false;
};

} // namespace winmtr::probe
