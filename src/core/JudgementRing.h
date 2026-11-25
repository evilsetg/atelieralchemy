#pragma once

// The Judgement Ring: a needle sweeps clockwise once around a ring. The ring has
// several hit areas; each ends in a narrow strike area. Press while the needle is
// inside the current hit area to score a Hit (or a Strike in the strike area).
// Missing an area - pressing outside it or letting the needle pass - fails that
// area, and by default ends the ring (every later area fails too).

#include <cstdint>
#include <random>
#include <vector>

namespace atelier
{
	enum class AreaResult : std::uint8_t
	{
		kPending,
		kMiss,
		kHit,
		kStrike,
	};

	struct RingArea
	{
		float start{ 0.0f };        // degrees clockwise from 12 o'clock
		float end{ 0.0f };          // > start
		float strikeStart{ 0.0f };  // strike area is [strikeStart, end]
	};

	struct RingParams
	{
		int   areaCount{ 3 };
		float hitWidthDeg{ 30.0f };
		float strikeWidthDeg{ 6.0f };
		float degreesPerSecond{ 180.0f };
		bool  missEndsRing{ true };
		bool  autoHit{ false };  // accessibility: every area is hit automatically (never strikes)
	};

	struct RingSummary
	{
		int  hits{ 0 };  // includes strikes
		int  strikes{ 0 };
		int  misses{ 0 };
		int  areas{ 0 };
		bool firstAreaMissed{ false };
		bool automatic{ false };  // Auto mode: hits earn no quality bonus

		[[nodiscard]] bool Perfect() const { return areas > 0 && strikes == areas; }
		[[nodiscard]] bool Clean() const { return areas > 0 && misses == 0; }
	};

	// Derives ring parameters from the player's skill and the recipe's difficulty.
	RingParams MakeRingParams(int a_areaCount, float a_skill, int a_recipeLevel, float a_bonusWidthDeg, float a_speedMult);

	class JudgementRing
	{
	public:
		JudgementRing() = default;

		void Start(const RingParams& a_params, std::uint32_t a_seed);
		// Advances the needle. Returns true while the ring is still running.
		bool Update(float a_seconds);
		void Press();

		[[nodiscard]] bool                           Running() const { return _running; }
		[[nodiscard]] bool                           Finished() const { return _finished; }
		[[nodiscard]] float                          Needle() const { return _needle; }
		[[nodiscard]] const std::vector<RingArea>&   Areas() const { return _areas; }
		[[nodiscard]] const std::vector<AreaResult>& Results() const { return _results; }
		[[nodiscard]] int                            CurrentArea() const { return _current; }
		[[nodiscard]] const RingParams&              Params() const { return _params; }
		[[nodiscard]] RingSummary                    Summary() const;
		// Time since the last judged area, for UI flashes. Negative if none yet.
		[[nodiscard]] float SinceLastJudgement() const { return _sinceJudged; }
		[[nodiscard]] int   LastJudgedArea() const { return _lastJudged; }

		// Exposed for tests: lay out areas deterministically.
		static std::vector<RingArea> Layout(const RingParams& a_params, std::mt19937& a_rng);

	private:
		void Judge(AreaResult a_result);
		void Finish();

		RingParams              _params;
		std::vector<RingArea>   _areas;
		std::vector<AreaResult> _results;
		float                   _needle{ 0.0f };
		int                     _current{ 0 };
		bool                    _running{ false };
		bool                    _finished{ false };
		float                   _sinceJudged{ -1.0f };
		int                     _lastJudged{ -1 };
	};
}
