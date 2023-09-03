#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#include <functional>
#include <map>
#include <memory>
#include <algorithm>
#include <array>
#include <random>

std::random_device rd;
std::mt19937 rng(rd());

template <typename T>
T lerp(T v0, T v1, float t) {
	return (1 - t) * v0 + t * v1;
}

bool PointInRect(const olc::vf2d point, const olc::vf2d& pos, const olc::vf2d& size) {
	if (point.x >= pos.x && point.y >= pos.y && point.x < pos.x + size.x && point.y < pos.y + size.y) {
		return true;
	}

	return false;
}

struct Card {
	olc::Pixel colorBack;
	olc::Pixel colorFront;
	olc::vf2d pos;
	olc::vf2d size;
	bool faceUp;
	float hue;
	float fmod;
};

enum class GameState {
	NONE,
	START_SCREEN,
	ROUND_START,
	SELECT_FIRST,
	ANIMATE_FIRST,
	SELECT_SECOND,
	ANIMATE_SECOND,
	TURN_END,
	MIXUP,
	SHUFFLE
};

std::vector<Card> the_cards;
GameState current_state = GameState::START_SCREEN;
int round_number = 0;
int turn_number = 1;
int field_width = 0;
int field_height = 0;
int first_card = -1;
int second_card = -1;
int score = 0;

std::array<olc::Pixel, 14> valid_colors = {
	olc::GREY,
	olc::GREEN,
	olc::YELLOW,
	olc::MAGENTA,
	olc::CYAN,
	olc::DARK_RED,
	olc::BLUE,
	olc::VERY_DARK_GREY,
	olc::DARK_GREEN,
	olc::DARK_YELLOW,
	olc::DARK_MAGENTA,
	olc::DARK_CYAN,
	olc::BLACK,
	olc::WHITE
};

float Ease(float x) {
	return -(std::cos(3.1415926f * x) - 1) / 2;
}

float RandFloat() {
	return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

struct State {
	olc::PixelGameEngine* pge;
	State(olc::PixelGameEngine* pge_) : pge(pge_) {};
	virtual ~State() = default;
	virtual void EnterState() {};
	virtual GameState OnUserUpdate(float fElapsedTime) = 0;
	virtual void ExitState() {};
};

struct StartScreenState : public State {
	StartScreenState(olc::PixelGameEngine* pge) : State(pge) {};

	olc::Renderable memory;

	olc::vf2d memory_pos[4] = {
		{32.0f, 64.0f},
		{32.0f, 128.0f},
		{224.0f, 128.0f},
		{224.0f, 64.0f}
	};

	olc::vf2d memory_uv[4] = {
		{0.0f, 0.0f},
		{0.0f, 1.0f},
		{1.0f, 1.0f},
		{1.0f, 0.0f}
	};

	olc::Pixel memory_color[4] = {
		olc::WHITE,
		olc::WHITE,
		olc::BLANK,
		olc::BLANK
	};

	void EnterState() override { 
		olc::vf2d text_size = pge->GetTextSize("MEMORY");

		memory.Create(text_size.x, text_size.y);

		pge->SetDrawTarget(memory.Sprite());
		pge->Clear(olc::BLANK);
		pge->DrawString({ 0,0 }, "MEMORY");
		memory.Decal()->Update();
		pge->SetDrawTarget((uint8_t)0);

	}

	GameState OnUserUpdate(float fElapsedTime) {
		GameState next_state = GameState::START_SCREEN;

		olc::vf2d button_pos = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() * 2.0f / 3.0f };
		olc::vf2d button_size = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() / 6.0f };

		pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);
		
		olc::vf2d text_size = pge->GetTextSize("Start");

		olc::vf2d scale = (button_size) / text_size;

		pge->DrawStringDecal(button_pos + olc::vf2d{2.0, 2.0}, "Start", olc::BLACK, scale);

		olc::Decal* m = memory.Decal();
		
		pge->DrawExplicitDecal(m, &memory_pos[0], memory_uv, memory_color);

		if (pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
				next_state = GameState::ROUND_START;
			}
		}

		return next_state;
	}
};

struct RoundStartState : public State {
	RoundStartState(olc::PixelGameEngine* pge) : State(pge) {}

	olc::vf2d card_size;
	olc::vf2d field_dim;

	//When we enter the state, populate the cards vector with new cards
	//and increment the round number
	void EnterState() override {
		turn_number = 1;
		round_number++;

		//int desired_pair_count = std::min(7 + round_number, 20);
		int desired_pair_count = 7 + round_number;
		int desired_card_count = desired_pair_count * 2;
		field_width = std::ceil(std::sqrt(desired_card_count));
		field_height = std::floor(std::sqrt(desired_card_count));

		//Ratio of card width over card height
		float ratio = 2.5f / 3.5f;

		float min_gap = 8.0f - std::min(7.0f, std::sqrtf(round_number) - 1);

		//float card_width = (pge->ScreenWidth() - (field_width + 1) * olc::vf2d{min_gap * ratio, min_gap}) / field_width;
		field_dim = { (float)field_width, (float)field_height };
		card_size = (pge->GetScreenSize() - olc::vf2d{0.0f, 2.0f} - ((field_dim + olc::vf2d{ 1.0f, 1.0f }) * olc::vf2d{ min_gap / ratio, min_gap })) / field_dim;

		float max_card_width = (pge->ScreenWidth() - (field_width + 1) * min_gap) / field_width;
		float max_card_height = (pge->ScreenHeight() - (field_height + 1) * min_gap) / field_height;

		float w_from_h = max_card_height * ratio;

		// We have the card size now
		if (w_from_h <= max_card_width) {
			card_size = olc::vf2d{ w_from_h, max_card_height };
		}
		else {
			card_size = olc::vf2d{ max_card_width, max_card_width / ratio };
		}

		//Calculate the card positions and gaps
		olc::vf2d gap = (pge->GetScreenSize() - card_size * field_dim) / (field_dim + olc::vi2d{ 1, 1 });

		the_cards.clear();

		std::vector<olc::Pixel> colors;

		int pair_count = (field_width * field_height) / 2;

		for (int i = 0; i < pair_count; i++) {
			auto color = valid_colors[i % valid_colors.size()];
			the_cards.push_back(Card{ olc::RED, color, olc::vf2d{}, card_size, false, 0.0, RandFloat()});
			the_cards.push_back(Card{ olc::RED, color, olc::vf2d{}, card_size, false, 0.0, RandFloat()});
		}


		std::shuffle(the_cards.begin(), the_cards.end(), rng);

		for (int x = 0; x < field_width; x++) {
			for (int y = 0; y < field_height; y++) {
				olc::vf2d pos = olc::vf2d{ (float)x, (float)y } * card_size + olc::vf2d{ (float)x + 1, (float)y + 1 } * gap + olc::vf2d{0.0f, 2.0f};
				auto idx = x * field_height + y;
				the_cards[idx].pos = pos;
			}
		}
		
	}

	GameState OnUserUpdate(float fElapsedTime) {
		return GameState::SELECT_FIRST;
	}
};

struct SelectFirstState : public State {
	SelectFirstState(olc::PixelGameEngine* pge) : State(pge) {};

	GameState OnUserUpdate(float fElapsedTime) override {
		
		//Draw the current cards
		for (const auto& c : the_cards) {
			if (PointInRect(pge->GetMousePos(), c.pos, c.size)) {
				olc::vf2d fudge = c.size * 0.03f;
				pge->FillRectDecal(c.pos - fudge, c.size + fudge * 2.0f, olc::GREY);
			}
			pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
		}

		//Test if a card is selected
		if (pge->GetMouse(0).bPressed) {
			for (int i = 0; i < the_cards.size(); i++) {
				const auto& c = the_cards[i];
				if (PointInRect(pge->GetMousePos(), c.pos, c.size)) {
					first_card = i;
					return GameState::ANIMATE_FIRST;
				}
			}
		}

		return GameState::SELECT_FIRST;
	}
};

struct SelectSecondState : public State {
	SelectSecondState(olc::PixelGameEngine* pge) : State(pge) {};

	GameState OnUserUpdate(float fElapsedTime) override {

		//Draw the current cards
		for (const auto& c : the_cards) {
			if (PointInRect(pge->GetMousePos(), c.pos, c.size)) {
				olc::vf2d fudge = c.size * 0.03f;
				pge->FillRectDecal(c.pos - fudge, c.size + fudge * 2.0f, olc::GREY);
			}
			pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
		}

		//Test if a card is selected
		if (pge->GetMouse(0).bPressed) {
			for (int i = 0; i < the_cards.size(); i++) {
				const auto& c = the_cards[i];
				if ((first_card != i) && PointInRect(pge->GetMousePos(), c.pos, c.size)) {
					second_card = i;
					return GameState::ANIMATE_SECOND;
				}
			}
		}

		return GameState::SELECT_SECOND;
	}
};

void DrawFlipAnimationHorizontal(float fTotalTime, Card& card, olc::PixelGameEngine* pge) {
	float t = std::fabsf(Ease(fTotalTime) - 0.5f) * 2.0f;
	olc::vf2d new_pos = card.pos + olc::vf2d{ (1.0f - t) * card.size.x / 2.0f, 0.0f };
	olc::vf2d new_size = olc::vf2d{ t * card.size.x, card.size.y };
	pge->FillRectDecal(new_pos, new_size, card.faceUp ? card.colorFront : card.colorBack);
}

struct AnimateFirstState : public State {
	AnimateFirstState(olc::PixelGameEngine* pge) : State(pge) {};

	float fTotalTime = 0.0f;

	void EnterState() override {
		fTotalTime = 0.0f;
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		//Accrue time
		fTotalTime += fElapsedTime * 1.2f;
		
		//Draw the current cards
		for (int i = 0; i < the_cards.size(); i++) {
			auto& c = the_cards[i];

			if (i != first_card) {
				pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
			}
			else {
				DrawFlipAnimationHorizontal(fTotalTime, c, pge);
				if (fTotalTime > 0.5f) {
					c.faceUp = true;
				}
			}
		}
		
		if (fTotalTime > 1.0f) {
			return GameState::SELECT_SECOND;
		}

		return GameState::ANIMATE_FIRST;
	}
};

struct AnimateSecondState : public State {
	AnimateSecondState(olc::PixelGameEngine* pge) : State(pge) {};

	float fTotalTime = 0.0f;

	void EnterState() override {
		fTotalTime = 0.0f;
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		//Accrue time
		fTotalTime += fElapsedTime * 1.2f;

		//Draw the current cards
		for (int i = 0; i < the_cards.size(); i++) {
			auto& c = the_cards[i];

			if (i != second_card) {
				pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
			}
			else {
				DrawFlipAnimationHorizontal(fTotalTime, c, pge);
				if (fTotalTime > 0.5f) {
					c.faceUp = true;
				}
			}
		}

		if (fTotalTime > 1.0f) {
			return GameState::TURN_END;
		}

		return GameState::ANIMATE_SECOND;
	}
};

struct TurnEndState : public State {
	TurnEndState(olc::PixelGameEngine* pge) : State(pge) {};

	bool did_match = false;
	float fTotalTime = 0.0f;

	void EnterState() override {
		did_match = the_cards[first_card].colorFront == the_cards[second_card].colorFront;
		fTotalTime = 0.0f;
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		fTotalTime += fElapsedTime * 1.2f;
		
		//Draw the current cards
		for (int i = 0; i < the_cards.size(); i++) {
			auto& c = the_cards[i];

			if ((i != first_card) && (i != second_card)) {
				pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
			}
			else {

				//If the cards matched, they should vanish
				if (did_match) {
					olc::vf2d new_pos = c.pos + olc::vf2d{Ease(fTotalTime) * c.size.x / 2.0f, 0.0f };
					olc::vf2d new_size = olc::vf2d{ (1.0f - Ease(fTotalTime)) * c.size.x, c.size.y};
					pge->FillRectDecal(new_pos, new_size, c.faceUp ? c.colorFront : c.colorBack);
				}
				else {
					float t = std::fabsf(Ease(fTotalTime) - 0.5f) * 2.0f;
					olc::vf2d new_pos = c.pos + olc::vf2d{ (1.0f - t) * c.size.x / 2.0f, 0.0f };
					olc::vf2d new_size = olc::vf2d{ t * c.size.x, c.size.y };
					if (fTotalTime > 0.5f) {
						c.faceUp = false;
					}

					pge->FillRectDecal(new_pos, new_size, c.faceUp ? c.colorFront : c.colorBack);
				}

			}
		}

		if (fTotalTime > 1.0f) {
			if (did_match) {
				the_cards.erase(the_cards.begin() + std::max(first_card, second_card));
				the_cards.erase(the_cards.begin() + std::min(first_card, second_card));
				score += 4;
			}
			else {
				score -= 1;
			}
			if (the_cards.size()) {
				if (round_number > 2) {
					return GameState::SHUFFLE;
				}
				else {
					return GameState::MIXUP;
				}
			}
			else {
				return GameState::ROUND_START;
			}
		}
		else {
			return GameState::TURN_END;
		}
	}

	void ExitState() override {
		turn_number++;
		first_card = -1;
		second_card = -1;
	}
};

struct MixupState : public State {
	MixupState(olc::PixelGameEngine* pge) : State(pge) {};

	enum class InnerState {
		PICK,
		ANIMATE,
		SKIP
	};

	InnerState inner_state = InnerState::PICK;

	int mixup_count = 0;
	float fTotalTime = 0.0f;
	bool pick_new = false;

	int card_one = -1;
	int card_two = -1;

	olc::vf2d new_pos_one = {};
	olc::vf2d new_pos_two = {};
	olc::vf2d old_pos_one = {};
	olc::vf2d old_pos_two = {};

	void EnterState() override {
		fTotalTime = 0.0f;
		mixup_count = std::floor(std::sqrtf(round_number + std::max(-1,turn_number - 5)));
		card_one = -1;
		card_two = -1;

		if (mixup_count > 0) {
			pick_new = true;
			inner_state = InnerState::PICK;
		}
		else {
			inner_state = InnerState::SKIP;
		}

	}

	void Pick() {
		card_one = std::uniform_int_distribution(0, (int)the_cards.size() - 1)(rng);
		do {
			card_two = std::uniform_int_distribution(0, (int)the_cards.size() - 1)(rng);
		} while (card_one == card_two);

		new_pos_one = the_cards[card_two].pos;
		old_pos_one = the_cards[card_one].pos;
		new_pos_two = the_cards[card_one].pos;
		old_pos_two = the_cards[card_two].pos;

		inner_state = InnerState::ANIMATE;
		fTotalTime = 0.0f;
	}

	void Animate(float fElapsedTime) {
		fTotalTime += fElapsedTime * (1 + (round_number - 1) * 0.5);

		auto& c1 = the_cards[card_one];
		auto& c2 = the_cards[card_two];

		auto t = Ease(fTotalTime);

		c1.pos = lerp(old_pos_one, new_pos_one, std::min(1.0f, t));
		c2.pos = lerp(old_pos_two, new_pos_two, std::min(1.0f, t));

		if (fTotalTime > 1.0f) {
			mixup_count--;
		}
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		for (const auto& c : the_cards) {
			pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
		}

		switch (inner_state) {
		case InnerState::PICK:
			Pick();
			inner_state = InnerState::ANIMATE;
			return GameState::MIXUP;
		case InnerState::ANIMATE:
			Animate(fElapsedTime);
			if (mixup_count != 0) {
				if (fTotalTime > 1.0f) {
					inner_state = InnerState::PICK;
				}
				return GameState::MIXUP;
			}
			else {
				return GameState::SELECT_FIRST;
			}
		case InnerState::SKIP:
			return GameState::SELECT_FIRST;
		}

	}
};

struct ShuffleState : public State {
	ShuffleState(olc::PixelGameEngine* pge) : State(pge) {}

	float fTotalTime = 0.0f;

	struct ShuffleData {
		int index;
		olc::vf2d old_pos;
		olc::vf2d new_pos;
	};

	std::vector<ShuffleData> sd;

	void EnterState() override {
		fTotalTime = 0.0f;
		int shuffle_count = std::min((int)the_cards.size(), (int)std::floor(round_number + std::sqrtf(turn_number)));



		std::vector<int> indices(the_cards.size());
		std::generate(indices.begin(), indices.end(), [n = 0]() mutable { return n++; });
		std::shuffle(indices.begin(), indices.end(), rng);


		sd.clear();

		for (int i = 0; i < shuffle_count; i++) {
			ShuffleData d;
			d.index = indices[i];
			d.old_pos = the_cards[d.index].pos;
			sd.push_back(d);
		}

		std::shuffle(indices.begin(), indices.begin() + shuffle_count, rng);

		for (int i = 0; i < shuffle_count; i++) {
			sd[i].new_pos = the_cards[indices[i]].pos;
		}
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		fTotalTime += fElapsedTime * (1 + (round_number - 1) * 0.5);
		auto t = Ease(fTotalTime);

		for (const auto& c : the_cards) {
			pge->FillRectDecal(c.pos, c.size, c.faceUp ? c.colorFront : c.colorBack);
		}

		for (const auto& d : sd) {
			Card& c = the_cards[d.index];
			c.pos = lerp(d.old_pos, d.new_pos, std::min(1.0f, t));
		}

		if (fTotalTime > 1.0f) {
			return GameState::MIXUP;
		}
		else {
			return GameState::SHUFFLE;
		}
	}
};

olc::Pixel FromHsv(float hue, float saturation, float value, const float alpha = 1)
{
	hue = std::clamp(hue, 0.0f, 360.0f);
	saturation = std::clamp(saturation, 0.0f, 1.0f);
	value = std::clamp(value, 0.0f, 1.0f);

	const float chroma = value * saturation;
	const float x = chroma * (1 - std::fabs(fmodf((hue / 60), 2) - 1));
	const float m = value - chroma;

	float rawRed, rawGreen, rawBlue;

	if (saturation == 0) {
		rawRed = rawGreen = rawBlue = value;
	}
	else if (hue < 60) {
		rawRed = chroma;
		rawGreen = x;
		rawBlue = 0;
	}
	else if (60 <= hue && hue < 120) {
		rawRed = x;
		rawGreen = chroma;
		rawBlue = 0;
	}
	else if (120 <= hue && hue < 180) {
		rawRed = 0;
		rawGreen = chroma;
		rawBlue = x;
	}
	else if (180 <= hue && hue < 240) {
		rawRed = 0;
		rawGreen = x;
		rawBlue = chroma;
	}
	else if (240 <= hue && hue < 300) {
		rawRed = x;
		rawGreen = 0;
		rawBlue = chroma;
	}
	else {
		rawRed = chroma;
		rawGreen = 0;
		rawBlue = x;
	}

	return olc::PixelF(rawRed + m, rawGreen + m, rawBlue + m, alpha);
}

void MessWithColors(float fElapsedTime) {
	for (auto& c : the_cards) {
		c.hue += fElapsedTime * c.fmod * 2.0;
		c.hue = fmodf(c.hue, 360.0f);
		
		
		c.colorBack = FromHsv(c.hue, 1.0f, 1.0f);
	}
}

// Override base class with your custom functionality
class MemoryGame : public olc::PixelGameEngine
{
public:
	MemoryGame()
	{
		// Name your application
		sAppName = "Example";
	}

	std::map<GameState, std::unique_ptr<State>> gameStates;


	GameState next_state = GameState::START_SCREEN;
	GameState prev_state = GameState::NONE;
	Card card;

public:
	bool OnUserCreate() override
	{
		card.colorBack = olc::RED;
		card.colorFront = olc::GREY;
		card.pos = { 16.0f, 16.0f };
		card.size = { 44.0f, 58.0f };
		card.faceUp = false;
		// Called once at the start, so create things here

		gameStates.insert(std::make_pair( GameState::START_SCREEN, std::make_unique<StartScreenState>(this) ));
		gameStates.insert(std::make_pair( GameState::ROUND_START, std::make_unique<RoundStartState>(this) ));
		gameStates.insert(std::make_pair( GameState::SELECT_FIRST, std::make_unique<SelectFirstState>(this) ));
		gameStates.insert(std::make_pair( GameState::ANIMATE_FIRST, std::make_unique<AnimateFirstState>(this) ));
		gameStates.insert(std::make_pair(GameState::SELECT_SECOND, std::make_unique<SelectSecondState>(this)));
		gameStates.insert(std::make_pair(GameState::ANIMATE_SECOND, std::make_unique<AnimateSecondState>(this)));
		gameStates.insert(std::make_pair(GameState::TURN_END, std::make_unique<TurnEndState>(this)));
		gameStates.insert(std::make_pair(GameState::MIXUP, std::make_unique<MixupState>(this)));
		gameStates.insert(std::make_pair(GameState::SHUFFLE, std::make_unique<ShuffleState>(this)));
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		FillRectDecal({ 0,0 }, { 256, 240 }, olc::DARK_BLUE);
		// Called once per frame, draws random coloured pixels
		

		const auto& state = gameStates.at(current_state);

		if (current_state != prev_state) {
			state->EnterState();
		}

		next_state = state->OnUserUpdate(fElapsedTime);

		if (next_state != current_state) {
			state->ExitState();
		}

		if (current_state != GameState::START_SCREEN) {
			std::string score_str = "Round: " + std::to_string(round_number) + "  Score: " + std::to_string(score);
			DrawStringDecal({ ScreenWidth() * 0.2f, 1.0f }, score_str);
			
			if (round_number > 1) {
				MessWithColors(fElapsedTime);
			}
		}

		prev_state = current_state;
		current_state = next_state;

		return true;
	}
};

int main()
{
	MemoryGame demo;
	if (demo.Construct(256, 240, 4, 4))
		demo.Start();
	return 0;
}
