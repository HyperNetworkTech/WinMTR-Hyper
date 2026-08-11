#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace winmtr::json {

class StringFieldParser final {
public:
	StringFieldParser(std::string_view input, std::string_view target) noexcept
		: input_(input), target_(target) {}

	[[nodiscard]] std::optional<std::string> parse()
	{
		if (input_.size() > maximum_input_bytes || !parse_object(0, true)) return std::nullopt;
		skip_space();
		return position_ == input_.size() ? result_ : std::nullopt;
	}

private:
	static constexpr std::size_t maximum_input_bytes = 1024u * 1024u;
	static constexpr std::size_t maximum_string_bytes = 4096u;
	static constexpr unsigned maximum_depth = 64;

	void skip_space() noexcept
	{
		while (position_ < input_.size()
			&& std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
			++position_;
		}
	}

	[[nodiscard]] bool consume(char expected) noexcept
	{
		skip_space();
		if (position_ >= input_.size() || input_[position_] != expected) return false;
		++position_;
		return true;
	}

	[[nodiscard]] static int hex_value(char digit) noexcept
	{
		if (digit >= '0' && digit <= '9') return digit - '0';
		if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
		if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
		return -1;
	}

	[[nodiscard]] bool unicode_escape(unsigned& value) noexcept
	{
		if (position_ + 4 > input_.size()) return false;
		value = 0;
		for (unsigned index = 0; index < 4; ++index) {
			const int digit = hex_value(input_[position_++]);
			if (digit < 0) return false;
			value = (value << 4u) | static_cast<unsigned>(digit);
		}
		return true;
	}

	static void append_utf8(std::string& output, unsigned code_point)
	{
		if (code_point <= 0x7f) {
			output.push_back(static_cast<char>(code_point));
		}
		else if (code_point <= 0x7ff) {
			output.push_back(static_cast<char>(0xc0u | (code_point >> 6u)));
			output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
		}
		else if (code_point <= 0xffff) {
			output.push_back(static_cast<char>(0xe0u | (code_point >> 12u)));
			output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
			output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
		}
		else {
			output.push_back(static_cast<char>(0xf0u | (code_point >> 18u)));
			output.push_back(static_cast<char>(0x80u | ((code_point >> 12u) & 0x3fu)));
			output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
			output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
		}
	}

	[[nodiscard]] std::optional<std::string> parse_string()
	{
		skip_space();
		if (position_ >= input_.size() || input_[position_++] != '"') return std::nullopt;
		std::string output;
		while (position_ < input_.size()) {
			const unsigned char value = static_cast<unsigned char>(input_[position_++]);
			if (value == '"') return output;
			if (value < 0x20) return std::nullopt;
			if (value != '\\') {
				output.push_back(static_cast<char>(value));
				if (output.size() > maximum_string_bytes) return std::nullopt;
				continue;
			}
			if (position_ >= input_.size()) return std::nullopt;
			switch (input_[position_++]) {
			case '"': output.push_back('"'); break;
			case '\\': output.push_back('\\'); break;
			case '/': output.push_back('/'); break;
			case 'b': output.push_back('\b'); break;
			case 'f': output.push_back('\f'); break;
			case 'n': output.push_back('\n'); break;
			case 'r': output.push_back('\r'); break;
			case 't': output.push_back('\t'); break;
			case 'u': {
				unsigned first = 0;
				if (!unicode_escape(first)) return std::nullopt;
				unsigned code_point = first;
				if (first >= 0xd800 && first <= 0xdbff) {
					if (position_ + 2 > input_.size() || input_[position_] != '\\'
						|| input_[position_ + 1] != 'u') return std::nullopt;
					position_ += 2;
					unsigned second = 0;
					if (!unicode_escape(second) || second < 0xdc00 || second > 0xdfff) {
						return std::nullopt;
					}
					code_point = 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
				}
				else if (first >= 0xdc00 && first <= 0xdfff) {
					return std::nullopt;
				}
				append_utf8(output, code_point);
				break;
			}
			default: return std::nullopt;
			}
			if (output.size() > maximum_string_bytes) return std::nullopt;
		}
		return std::nullopt;
	}

	[[nodiscard]] bool parse_object(unsigned depth, bool root)
	{
		if (depth > maximum_depth || !consume('{')) return false;
		std::unordered_set<std::string> keys;
		skip_space();
		if (position_ < input_.size() && input_[position_] == '}') {
			++position_;
			return true;
		}
		for (;;) {
			auto key = parse_string();
			if (!key || !keys.insert(*key).second || !consume(':')) return false;
			if (root && *key == target_) {
				skip_space();
				if (position_ < input_.size() && input_[position_] == '"') {
					result_ = parse_string();
					if (!result_) return false;
				}
				else if (!parse_value(depth + 1)) return false;
			}
			else if (!parse_value(depth + 1)) return false;
			skip_space();
			if (position_ < input_.size() && input_[position_] == '}') {
				++position_;
				return true;
			}
			if (!consume(',')) return false;
		}
	}

	[[nodiscard]] bool parse_array(unsigned depth)
	{
		if (depth > maximum_depth || !consume('[')) return false;
		skip_space();
		if (position_ < input_.size() && input_[position_] == ']') {
			++position_;
			return true;
		}
		for (;;) {
			if (!parse_value(depth + 1)) return false;
			skip_space();
			if (position_ < input_.size() && input_[position_] == ']') {
				++position_;
				return true;
			}
			if (!consume(',')) return false;
		}
	}

	[[nodiscard]] bool parse_number() noexcept
	{
		const auto start = position_;
		if (position_ < input_.size() && input_[position_] == '-') ++position_;
		if (position_ >= input_.size()) return false;
		if (input_[position_] == '0') ++position_;
		else {
			if (input_[position_] < '1' || input_[position_] > '9') return false;
			while (position_ < input_.size() && std::isdigit(
				static_cast<unsigned char>(input_[position_])) != 0) ++position_;
		}
		if (position_ < input_.size() && input_[position_] == '.') {
			++position_;
			const auto fraction = position_;
			while (position_ < input_.size() && std::isdigit(
				static_cast<unsigned char>(input_[position_])) != 0) ++position_;
			if (fraction == position_) return false;
		}
		if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
			++position_;
			if (position_ < input_.size() && (input_[position_] == '+'
				|| input_[position_] == '-')) ++position_;
			const auto exponent = position_;
			while (position_ < input_.size() && std::isdigit(
				static_cast<unsigned char>(input_[position_])) != 0) ++position_;
			if (exponent == position_) return false;
		}
		return position_ > start;
	}

	[[nodiscard]] bool literal(std::string_view value) noexcept
	{
		if (input_.substr(position_, value.size()) != value) return false;
		position_ += value.size();
		return true;
	}

	[[nodiscard]] bool parse_value(unsigned depth)
	{
		if (depth > maximum_depth) return false;
		skip_space();
		if (position_ >= input_.size()) return false;
		switch (input_[position_]) {
		case '"': return parse_string().has_value();
		case '{': return parse_object(depth, false);
		case '[': return parse_array(depth);
		case 't': return literal("true");
		case 'f': return literal("false");
		case 'n': return literal("null");
		default: return parse_number();
		}
	}

	std::string_view input_;
	std::string_view target_;
	std::size_t position_ = 0;
	std::optional<std::string> result_;
};

[[nodiscard]] inline std::optional<std::string> get_string(
	std::string_view json, std::string_view key)
{
	return StringFieldParser(json, key).parse();
}

} // namespace winmtr::json
