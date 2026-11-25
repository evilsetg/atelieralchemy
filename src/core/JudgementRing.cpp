#include "core/JudgementRing.h"

#include <algorithm>

namespace atelier
{
	namespace
	{
		constexpr float kLeadIn = 25.0f;   // degrees before the first area can start
		constexpr float kLeadOut = 345.0f;  // last area must end before this
		constexpr float kMinGap = 4.0f;
	}

	RingParams MakeRingParams(int a_areaCount, float a_skill, int a_recipeLevel, float a_bonusWidthDeg, float a_speedMult)
	{
		RingParams params;
		params.areaCount = std::clamp(a_areaCount, 1, 8);

		const float segment = (kLeadOut - kLeadIn) / static_cast<float>(params.areaCount);
		const float maxWidth = segment - kMinGap;
		params.hitWidthDeg = std::clamp(24.0f + a_skill * 0.16f + a_bonusWidthDeg, 8.0f, maxWidth);
		params.strikeWidthDeg = std::clamp(4.0f + a_skill * 0.05f, 3.0f, params.hitWidthDeg * 0.4f);

		const float difficulty = std::max(0.0f, static_cast<float>(a_recipeLevel) - a_skill);
		params.degreesPerSecond = std::clamp(150.0f * (1.0f + difficulty / 80.0f) * a_speedMult, 90.0f, 360.0f);
		return params;
	}

	namespace
	{
		// Places a pattern's areas: sizes from the multipliers, centers from the pattern plus a
		// little jitter, then pushed apart so they never overlap and always fit the ring.
		std::vector<RingArea> LayoutShape(const RingParams& a_params, std::mt19937& a_rng)
		{
			const float span = kLeadOut - kLeadIn;
			const auto  count = a_params.shape.size();
			std::uniform_real_distribution<float> jitter(-span * 0.015f, span * 0.015f);

			struct Slot
			{
				float center;
				float width;
				float strike;
			};
			std::vector<Slot> slots;
			for (const auto& area : a_params.shape) {
				const float width = std::clamp(a_params.hitWidthDeg * area.width, 6.0f, 100.0f);
				slots.push_back({ kLeadIn + span * std::clamp(area.center, 0.0f, 1.0f) + jitter(a_rng), width, a_params.strikeWidthDeg * area.strike });
			}
			std::ranges::sort(slots, {}, &Slot::center);

			// Shrink everything proportionally if the pattern doesn't fit at this size.
			float total = kMinGap * static_cast<float>(count - 1);
			for (const auto& slot : slots) {
				total += slot.width;
			}
			if (total > span) {
				const float scale = (span - kMinGap * static_cast<float>(count - 1)) / (total - kMinGap * static_cast<float>(count - 1));
				for (auto& slot : slots) {
					slot.width *= scale;
				}
			}

			std::vector<RingArea> areas(count);
			for (std::size_t i = 0; i < count; ++i) {
				areas[i].start = std::max(slots[i].center - slots[i].width * 0.5f, i == 0 ? kLeadIn : areas[i - 1].end + kMinGap);
				areas[i].end = areas[i].start + slots[i].width;
			}
			// Pull back from the end if the forward pass ran past it.
			for (std::size_t i = count; i-- > 0;) {
				const float limit = i + 1 == count ? kLeadOut : areas[i + 1].start - kMinGap;
				if (areas[i].end > limit) {
					areas[i].end = limit;
					areas[i].start = limit - slots[i].width;
				}
			}
			for (std::size_t i = 0; i < count; ++i) {
				const float width = areas[i].end - areas[i].start;
				areas[i].strikeStart = areas[i].end - std::clamp(slots[i].strike, 2.0f, width * 0.6f);
			}
			return areas;
		}
	}

	std::vector<RingArea> JudgementRing::Layout(const RingParams& a_params, std::mt19937& a_rng)
	{
		if (!a_params.shape.empty()) {
			return LayoutShape(a_params, a_rng);
		}
		std::vector<RingArea> areas;
		const int             count = std::max(a_params.areaCount, 1);
		const float           segment = (kLeadOut - kLeadIn) / static_cast<float>(count);
		const float           width = std::min(a_params.hitWidthDeg, segment - kMinGap);
		const float           strike = std::min(a_params.strikeWidthDeg, width);

		for (int i = 0; i < count; ++i) {
			const float                           segStart = kLeadIn + segment * static_cast<float>(i);
			const float                           slack = std::max(segment - width - kMinGap, 0.0f);
			std::uniform_real_distribution<float> jitter(0.0f, slack);
			RingArea                              area;
			area.start = segStart + jitter(a_rng);
			area.end = area.start + width;
			area.strikeStart = area.end - strike;
			areas.push_back(area);
		}
		return areas;
	}

	void JudgementRing::Start(const RingParams& a_params, std::uint32_t a_seed)
	{
		std::mt19937 rng(a_seed);
		_params = a_params;
		_areas = Layout(a_params, rng);
		_results.assign(_areas.size(), AreaResult::kPending);
		_needle = 0.0f;
		_current = 0;
		_running = true;
		_finished = false;
		_sinceJudged = -1.0f;
		_lastJudged = -1;
	}

	bool JudgementRing::Update(float a_seconds)
	{
		if (_sinceJudged >= 0.0f) {
			_sinceJudged += a_seconds;
		}
		if (!_running) {
			return false;
		}

		_needle += _params.degreesPerSecond * a_seconds;

		while (_running && _current < static_cast<int>(_areas.size())) {
			const auto& area = _areas[static_cast<std::size_t>(_current)];
			if (_params.autoHit && _needle >= (area.start + area.end) * 0.5f) {
				Judge(AreaResult::kHit);
				continue;
			}
			if (_needle > area.end) {
				Judge(AreaResult::kMiss);
				continue;
			}
			break;
		}

		if (_running && _needle >= 360.0f) {
			_needle = 360.0f;
			Finish();
		}
		return _running;
	}

	void JudgementRing::Press()
	{
		if (!_running || _params.autoHit || _current >= static_cast<int>(_areas.size())) {
			return;
		}

		const auto& area = _areas[static_cast<std::size_t>(_current)];
		if (_needle < area.start) {
			Judge(AreaResult::kMiss);  // too early
		} else if (_needle >= area.strikeStart) {
			Judge(AreaResult::kStrike);
		} else {
			Judge(AreaResult::kHit);
		}
	}

	void JudgementRing::Judge(AreaResult a_result)
	{
		_results[static_cast<std::size_t>(_current)] = a_result;
		_lastJudged = _current;
		_sinceJudged = 0.0f;
		++_current;

		if ((a_result == AreaResult::kMiss && _params.missEndsRing) || _current >= static_cast<int>(_areas.size())) {
			Finish();
		}
	}

	void JudgementRing::Finish()
	{
		_running = false;
		_finished = true;
		for (auto& result : _results) {
			if (result == AreaResult::kPending) {
				result = AreaResult::kMiss;
			}
		}
	}

	RingSummary JudgementRing::Summary() const
	{
		RingSummary summary;
		summary.areas = static_cast<int>(_results.size());
		summary.automatic = _params.autoHit;
		for (std::size_t i = 0; i < _results.size(); ++i) {
			switch (_results[i]) {
			case AreaResult::kStrike:
				++summary.strikes;
				[[fallthrough]];
			case AreaResult::kHit:
				++summary.hits;
				break;
			case AreaResult::kMiss:
			case AreaResult::kPending:
				++summary.misses;
				if (i == 0) {
					summary.firstAreaMissed = true;
				}
				break;
			}
		}
		return summary;
	}
}
