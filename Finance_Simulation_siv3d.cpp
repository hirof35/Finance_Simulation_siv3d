# include <Siv3D.hpp>

// --- データ構造 ---
struct AppConfig {
	double initialBalance = 2000.0;
	double monthlyInvest = 10.0;
	double annualReturn = 0.05;
	double annualStdDev = 0.15;
	double annualInflation = 0.02;
	bool useGuardrail = true;

	void save(const FilePath& path) const {
		JSON json;
		json[U"initialBalance"] = initialBalance;
		json[U"monthlyInvest"] = monthlyInvest;
		json[U"annualReturn"] = annualReturn;
		json[U"annualStdDev"] = annualStdDev;
		json[U"annualInflation"] = annualInflation;
		json[U"useGuardrail"] = useGuardrail;
		json.save(path);
	}

	void load(const FilePath& path) {
		if (const JSON json = JSON::Load(path)) {
			initialBalance = json[U"initialBalance"].get<double>();
			monthlyInvest = json[U"monthlyInvest"].get<double>();
			annualReturn = json[U"annualReturn"].get<double>();
			annualStdDev = json[U"annualStdDev"].get<double>();
			annualInflation = json[U"annualInflation"].get<double>();
			useGuardrail = json[U"useGuardrail"].get<bool>();
		}
	}
};

struct SimulationResult {
	Array<double> realPath; // インフレ調整後の推移
	bool wentBust = false;
	double maxDrawdown = 0.0;
};

// --- シミュレーションコアロジック ---
SimulationResult RunSingleSim(const AppConfig& conf, int32 totalMonths) {
	SimulationResult res;
	double nominal = conf.initialBalance;
	double priceIndex = 1.0;
	double peak = conf.initialBalance;
	bool isSavingMode = false;

	for (int32 m = 0; m < totalMonths; ++m) {
		// 1. 乱数生成（リターンとインフレ）
		double mReturn = 1.0 + Random(conf.annualReturn / 12.0, conf.annualStdDev / std::sqrt(12.0));
		double mInf = 1.0 + Random(conf.annualInflation / 12.0, 0.01 / std::sqrt(12.0));

		// 2. 取り崩し/積立（ガードレール戦略）
		double cashflow = conf.monthlyInvest;
		if (conf.useGuardrail && isSavingMode && cashflow < 0) cashflow *= 0.7; // 支出なら30%カット

		// 3. 資産更新
		nominal = (nominal + cashflow) * mReturn;
		priceIndex *= mInf;
		double realValue = nominal / priceIndex;

		if (realValue <= 0) { realValue = 0; res.wentBust = true; }
		res.realPath << realValue;

		// 4. 統計更新
		peak = Max(peak, realValue);
		double dd = (peak > 0) ? (realValue - peak) / peak : 0;
		res.maxDrawdown = Min(res.maxDrawdown, dd);

		// 5. ガードレール判定 (1年ごとに更新)
		if (m % 12 == 11) isSavingMode = (mReturn < 1.0);
		if (res.wentBust) break;
	}
	return res;
}

void Main() {
	Window::Resize(1280, 720);
	Window::SetTitle(U"Professional Finance Simulator");
	// 標準的なフォントを作成（サイズ20）
	const Font font{ 20 };
	AppConfig config;
	const FilePath configPath = U"config.json";
	if (FileSystem::Exists(configPath)) config.load(configPath);

	Array<SimulationResult> results;
	double successRate = 0;
	const int32 years = 30;

	auto ExecuteAll = [&]() {
		results.clear();
		int32 success = 0;
		for (int32 i = 0; i < 1000; ++i) {
			auto r = RunSingleSim(config, years * 12);
			if (!r.wentBust) success++;
			results << r;
		}
		successRate = (double)success / results.size();
		};

	ExecuteAll();

	while (System::Update()) {
		// --- UI Panel ---
		Rect{ 0, 0, 320, 720 }.draw(ColorF{ 0.15 });
		bool changed = false;
		changed |= SimpleGUI::Slider(U"初期資産: {:.0f}万"_fmt(config.initialBalance), config.initialBalance, 0, 5000, Vec2{ 20, 40 }, 120, 140);
		changed |= SimpleGUI::Slider(U"毎月収支: {:.1f}万"_fmt(config.monthlyInvest), config.monthlyInvest, -50, 50, Vec2{ 20, 80 }, 120, 140);
		changed |= SimpleGUI::Slider(U"期待利回: {:.1f}%"_fmt(config.annualReturn * 100), config.annualReturn, -0.05, 0.2, Vec2{ 20, 120 }, 120, 140);
		changed |= SimpleGUI::Slider(U"リスク: {:.1f}%"_fmt(config.annualStdDev * 100), config.annualStdDev, 0, 0.4, Vec2{ 20, 160 }, 120, 140);
		changed |= SimpleGUI::Slider(U"インフレ: {:.1f}%"_fmt(config.annualInflation * 100), config.annualInflation, -0.02, 0.1, Vec2{ 20, 200 }, 120, 140);
		changed |= SimpleGUI::CheckBox(config.useGuardrail, U"ガードレール戦略", Vec2{ 20, 240 });

		if (changed) ExecuteAll();

		if (SimpleGUI::Button(U"設定を保存", Vec2{ 20, 280 })) config.save(configPath);

		if (SimpleGUI::Button(U"CSVに出力", Vec2{ 160, 280 }))
		{
			CSV csv;
			// ヘッダー行の追加
			csv.write(U"TrialID");
			csv.write(U"FinalRealValue");
			csv.write(U"MaxDrawdown");
			csv.write(U"IsBust");
			csv.newLine();

			for (size_t i = 0; i < results.size(); ++i)
			{
				// 各列のデータを1つずつ書き込む
				csv.write(Format(i));                           // ID
				csv.write(Format(results[i].realPath.back()));  // 最終資産
				csv.write(Format(results[i].maxDrawdown));      // 最大ドローダウン
				csv.write(Format(results[i].wentBust ? 1 : 0)); // 倒産フラグ

				csv.newLine(); // 行を改める
			}

			if (csv.save(U"sim_result.csv"))
			{
				Print << U"CSV saved!";
			}
		}

		// --- 描画の基準座標 ---
		const double panelWidth = 320.0;
		const double graphX = panelWidth + 60.0; // 左パネルから60ピクセル離す
		const double graphY = 580.0;             // 下から140ピクセル上の位置を「0」とする
		const double graphWidth = Scene::Width() - graphX - 40.0;
		const double scaleX = graphWidth / (years * 12);
		const double scaleY = 0.04;              // 1万円あたりのピクセル数（高さ調整）

		// --- 1. グリッドと目盛りの描画 ---
		for (int i = 0; i <= 6; ++i) {
			double y = graphY - (i * 2000 * scaleY);
			Line{ graphX, y, graphX + graphWidth, y }.draw(1, ColorF{ 0.3 });
			// 目盛り（万単位）をグラフのすぐ左に配置
			font(U"{}万"_fmt(i * 2000)).draw(Arg::rightCenter = Vec2{ graphX - 10, y }, ColorF{ 0.6 });
		}

		// --- 2. シミュレーションパスの描画 ---
		for (size_t i = 0; i < Min<size_t>(50, results.size()); ++i) {
			const auto& path = results[i].realPath;
			for (size_t j = 1; j < path.size(); ++j) {
				Line{ graphX + (j - 1) * scaleX, graphY - path[j - 1] * scaleY,
					  graphX + j * scaleX, graphY - path[j] * scaleY }
				.draw(1, ColorF{ 0.4, 0.7, 1.0, 0.2 });
			}
		}

		// --- 3. 右上の統計ボックス (RectFでエラー回避) ---
		const RectF statsRect{ Scene::Width() - 280, 20, 260, 120 };
		statsRect.draw(ColorF{ 0.2, 0.9 });
		font(U"30年生存率").draw(statsRect.x + 20, statsRect.y + 15, Palette::White);
		// 生存率の数値を大きく表示
		font(U"{:.1f} %"_fmt(successRate * 100)).draw(40, Arg::topLeft = Vec2{ statsRect.x + 20, statsRect.y + 45 },
			(successRate > 0.8 ? Palette::Lime : Palette::Red));
	}
}
