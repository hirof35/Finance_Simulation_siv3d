# include <Siv3D.hpp>

// --- 共通データ構造 ---
struct ScoreRecord {
	int32 score;
	String name;
	String date;

	// デフォルトコンストラクタ
	ScoreRecord() = default;

	// 初期化用コンストラクタ
	ScoreRecord(int32 _score, const String& _name, const String& _date)
		: score(_score), name(_name), date(_date) {
	}

	void write(JSON& json) const {
		json[U"score"] = score;
		json[U"name"] = name;
		json[U"date"] = date;
	}
};

struct MyData {
	int32 lastScore = 0;
};

using MyStack = SceneManager<String, MyData>;

// --- エフェクト定義 ---
struct SparkEffect : IEffect {
	Vec2 m_pos;
	Vec2 m_velocity;
	SparkEffect(const Vec2& pos) : m_pos{ pos }, m_velocity{ RandomVec2(400.0) } {}
	bool update(double t) override {
		m_velocity.y += 800.0 * Scene::DeltaTime();
		m_pos += m_velocity * Scene::DeltaTime();
		Circle{ m_pos, Max(0.0, 10.0 * (1.0 - t)) }.draw(HSV{ 40, 0.8, 1.0, 1.0 - t });
		return t < 1.0;
	}
};

struct Confetti : IEffect {
	Vec2 m_pos; Vec2 m_vel; double m_angle; double m_angVel; ColorF m_color;
	Confetti(const Vec2& p) : m_pos{ p }, m_vel{ Random(-200.0, 200.0), Random(-600.0, -200.0) },
		m_angle{ Random(Math::TwoPi) }, m_angVel{ Random(-10.0, 10.0) }, m_color{ HSV{ Random(360.0), 0.7, 1.0 } } {
	}
	bool update(double t) override {
		m_vel.y += 400.0 * Scene::DeltaTime();
		m_pos += m_vel * Scene::DeltaTime();
		m_angle += m_angVel * Scene::DeltaTime();
		RectF{ Arg::center = m_pos, 12 }.rotated(m_angle).draw(m_color.withAlpha(1.0 - t));
		return t < 2.0;
	}
};

// --- シーン：タイトル ---
class Title : public MyStack::Scene {
public:
	Title(const InitData& init) : IScene{ init } {}
	void update() override {
		if (SimpleGUI::Button(U"ゲーム開始", Vec2{ 500, 400 }, 280)) changeScene(U"Game");
		if (SimpleGUI::Button(U"ランキング", Vec2{ 500, 480 }, 280)) changeScene(U"Ranking");
	}
	void draw() const override {
		Scene::SetBackground(Palette::Skyblue);
		FontAsset(U"TitleFont")(U"投擲マスター Siv3D").drawAt(Scene::Center().x, 200, Palette::Black);
	}
};

// --- シーン：ゲーム ---
class Game : public MyStack::Scene {
	Vec2 ballPos = Vec2{ 100, 600 };
	Vec2 ballVel{ 0, 0 };
	bool isFlying = false;
	int32 score = 0;
	int32 combo = 0;
	bool isBigBall = false;
	double wind = Random(-100.0, 100.0);
	Circle target{ 1100, 500, 60 };
	Rect obstacle{ 550, 200, 40, 300 };
	Effect effect;

public:
	Game(const InitData& init) : IScene{ init } {}
	void update() override {
		double dt = Scene::DeltaTime();
		if (!isFlying) {
			if (MouseL.down()) {
				ballVel = (Cursor::Pos() - Vec2{ 100, 600 }) * 2.2;
				isFlying = true;
			}
		}
		else {
			ballVel.y += 980.0 * dt;
			ballVel.x += wind * dt;
			ballPos += ballVel * dt;
			double r = isBigBall ? 45.0 : 15.0;

			if (obstacle.intersects(Circle{ ballPos, r })) {
				// 1. 反射処理（速度を反転させる）
				if (ballPos.x < obstacle.x || ballPos.x > obstacle.x + obstacle.w) {
					ballVel.x *= -0.7; // 左右の壁に当たった
				}
				else {
					ballVel.y *= -0.7; // 上下の壁に当たった
				}

				// 2. ★重要：めり込み防止（弾を衝突前の位置に少し戻す）
				ballPos -= ballVel * dt * 1.5; // 反転した速度を使って外側に押し出す

				// 3. 火花を散らす
				effect.add<SparkEffect>(ballPos);
			}

			if (target.intersects(Circle{ ballPos, r })) {
				score++; combo++;
				isBigBall = (combo % 3 == 0);
				for (int i = 0; i < 20; ++i) effect.add<SparkEffect>(ballPos);
				target.setCenter(Random(600, 1150), Random(150, 550));
				target.r = Max(20.0, 60.0 - score * 2);
				wind = Random(-100.0 - score * 20.0, 100.0 + score * 20.0);
				resetBall();
			}

			if (ballPos.y > 720 || ballPos.x < 0 || ballPos.x > 1280) {
				if (score > 0) {
					getData().lastScore = score;
					changeScene(U"Ranking");
				}
				combo = 0; isBigBall = false; resetBall();
			}
		}
	}

	void resetBall() { isFlying = false; ballPos = { 100, 600 }; }

	void draw() const override {
		Scene::SetBackground(Palette::Skyblue);
		Rect{ 0, 600, 1280, 120 }.draw(Palette::Forestgreen);
		obstacle.draw(Palette::Gray);
		target.draw(Palette::Red).drawFrame(2, 2, Palette::White);
		Circle{ ballPos, isBigBall ? 45.0 : 15.0 }.draw(isBigBall ? Palette::Gold : Palette::White);
		if (!isFlying)
			Line{ Vec2{ 100, 600 }, Cursor::Pos() }.drawArrow(10, Vec2{ 20, 20 }, isBigBall ? Palette::Gold : Palette::Orange);
		FontAsset(U"MainFont")(U"Score: {}  Combo: {}"_fmt(score, combo)).draw(20, 20, Palette::Black);
		FontAsset(U"MainFont")(U"Wind: {:.1f}"_fmt(wind)).drawAt(640, 40, Palette::Darkblue);
		effect.update();
	}
};

// --- シーン：ランキング ---
class Ranking : public MyStack::Scene {
	Array<ScoreRecord> ranking;
	TextEditState nameEdit;
	bool isSaved = false;
	Effect effect;

public:
	Ranking(const InitData& init) : IScene{ init } {
		ranking = Load();
		nameEdit.text = U"Player";
	}

	static Array<ScoreRecord> Load() {
		Array<ScoreRecord> res;
		const JSON j = JSON::Load(U"ranking.json");
		if (j) {
			for (const auto& i : j[U"ranking"].arrayView()) {
				res.push_back(ScoreRecord{
					i[U"score"].get<int32>(),
					i[U"name"].get<String>(),
					i[U"date"].get<String>()
				});
			}
		}
		return res;
	}

	void save() {
		ranking.push_back(ScoreRecord{ getData().lastScore, nameEdit.text, DateTime::Now().format() });
		// スコアで降順ソート
// --- 174行目付近の修正後 ---
// 2つの要素 a, b を比較し、aの方がスコアが高い場合に true を返す（降順）
		ranking.sort_by([](const ScoreRecord& a, const ScoreRecord& b) {
			return a.score > b.score;
		});		if (ranking.size() > 5) ranking.resize(5);

		// 1位なら紙吹雪
		if (ranking.size() > 0 && ranking[0].score == getData().lastScore) {
			for (int i = 0; i < 100; ++i) effect.add<Confetti>(Vec2{ Random(1280), 750 });
		}

		JSON j;
		for (auto& r : ranking) {
			JSON i; r.write(i); j[U"ranking"].push_back(i);
		}
		j.save(U"ranking.json");
		isSaved = true;
	}

	void update() override {
		if (!isSaved) {
			SimpleGUI::TextBox(nameEdit, Vec2{ 440, 120 }, 400);
			if (SimpleGUI::Button(U"登録", Vec2{ 850, 120 })) {
				save();
			}
		}
		else if (SimpleGUI::Button(U"タイトルへ", Vec2{ 540, 600 })) {
			changeScene(U"Title");
		}
	}

	void draw() const override {
		Scene::SetBackground(Palette::Black);
		FontAsset(U"TitleFont")(U"RANKING").drawAt(640, 60, Palette::Orange);

		if (!isSaved) {
			FontAsset(U"MainFont")(U"今回のスコア: {}"_fmt(getData().lastScore)).drawAt(640, 140);
		}

		for (auto [i, r] : Indexed(ranking)) {
			FontAsset(U"MainFont")(U"{}位: {:>4} pts - {} ({})"_fmt(i + 1, r.score, r.name, r.date))
				.draw(150, 220 + i * 50, i == 0 ? Palette::Gold : Palette::White);
		}
		effect.update();
	}
};

void Main() {
	FontAsset::Register(U"TitleFont", 60, Typeface::Heavy);
	FontAsset::Register(U"MainFont", 30, Typeface::Bold);
	MyStack manager;
	manager.add<Title>(U"Title");
	manager.add<Game>(U"Game");
	manager.add<Ranking>(U"Ranking");
	while (System::Update()) if (!manager.update()) break;
}
