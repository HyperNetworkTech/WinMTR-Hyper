#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace winmtr::route {

struct RoutePolicyConfig final {
	unsigned start_ttl = 1;
	unsigned minimum_ttl = 0;
	unsigned max_hops = 30;
	unsigned unknown_host_limit = 12;
	unsigned exploration_period = 10;
	unsigned exploration_frontier_ttls = 5;
	unsigned shrink_confirmations = 2;
};

class RoutePolicy final {
public:
	explicit RoutePolicy(RoutePolicyConfig config) noexcept
		: config_(normalize(config))
	{
		reset();
	}

	void reset() noexcept
	{
		exploring_ = true;
		initial_discovery_ = true;
		highest_response_ttl_ = 0;
		destination_ttl_ = 0;
		stable_ceiling_ = config_.max_hops;
		shrink_candidate_ = 0;
		shrink_confirmations_ = 0;
	}

	void note_reply(unsigned ttl, bool destination) noexcept
	{
		if (ttl < config_.start_ttl || ttl > config_.max_hops) return;
		highest_response_ttl_ = std::max(highest_response_ttl_, ttl);
		if (destination) {
			destination_ttl_ = destination_ttl_ == 0
				? ttl
				: std::min(destination_ttl_, ttl);
		}
	}

	// Returns a new active TTL ceiling only when the scheduler must change it.
	[[nodiscard]] std::optional<unsigned> complete_cycle(
		std::uint64_t completed_cycles) noexcept
	{
		std::optional<unsigned> next;
		if (exploring_) {
			const auto normal = normal_ceiling();
			if (initial_discovery_) {
				stable_ceiling_ = normal;
				initial_discovery_ = false;
				clear_shrink_candidate();
			}
			else if (normal < stable_ceiling_) {
				if (shrink_candidate_ == normal) ++shrink_confirmations_;
				else {
					shrink_candidate_ = normal;
					shrink_confirmations_ = 1;
				}
				if (shrink_confirmations_ >= config_.shrink_confirmations) {
					stable_ceiling_ = normal;
					clear_shrink_candidate();
				}
			}
			else {
				stable_ceiling_ = normal;
				clear_shrink_candidate();
			}
			exploring_ = false;
			next = stable_ceiling_;
		}

		if (config_.exploration_period != 0 && completed_cycles != 0
			&& completed_cycles % config_.exploration_period == 0
			&& stable_ceiling_ < config_.max_hops) {
			exploring_ = true;
			highest_response_ttl_ = 0;
			destination_ttl_ = 0;
			next = std::min(config_.max_hops,
				stable_ceiling_ + config_.exploration_frontier_ttls);
		}
		return next;
	}

	[[nodiscard]] unsigned destination_ttl() const noexcept { return destination_ttl_; }
	[[nodiscard]] unsigned stable_ceiling() const noexcept { return stable_ceiling_; }
	[[nodiscard]] bool exploring() const noexcept { return exploring_; }

private:
	[[nodiscard]] static RoutePolicyConfig normalize(RoutePolicyConfig value) noexcept
	{
		value.start_ttl = std::max(1u, value.start_ttl);
		value.max_hops = std::max(value.start_ttl, value.max_hops);
		value.minimum_ttl = std::min(value.minimum_ttl, value.max_hops);
		value.unknown_host_limit = std::max(1u, value.unknown_host_limit);
		value.shrink_confirmations = std::max(1u, value.shrink_confirmations);
		return value;
	}

	[[nodiscard]] unsigned mandatory_ttl() const noexcept
	{
		return std::max(config_.start_ttl, config_.minimum_ttl);
	}

	[[nodiscard]] unsigned normal_ceiling() const noexcept
	{
		if (destination_ttl_ != 0) {
			return std::max(mandatory_ttl(), destination_ttl_);
		}
		const auto tail_origin = highest_response_ttl_ == 0
			? config_.start_ttl - 1u
			: highest_response_ttl_;
		return std::min(config_.max_hops,
			std::max(mandatory_ttl(), tail_origin + config_.unknown_host_limit));
	}

	void clear_shrink_candidate() noexcept
	{
		shrink_candidate_ = 0;
		shrink_confirmations_ = 0;
	}

	RoutePolicyConfig config_;
	bool exploring_ = true;
	bool initial_discovery_ = true;
	unsigned highest_response_ttl_ = 0;
	unsigned destination_ttl_ = 0;
	unsigned stable_ceiling_ = 0;
	unsigned shrink_candidate_ = 0;
	unsigned shrink_confirmations_ = 0;
};

} // namespace winmtr::route
