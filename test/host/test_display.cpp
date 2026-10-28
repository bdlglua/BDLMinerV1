// Runs the production renderer with the production LovyanGFX fonts.
// Native sprites replace the physical LCD; no firmware or miner is simulated.
#include "ui_view.h"
#include "ui_font.h"
#include "i18n.h"
#include "settings.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>

using namespace ui_view;
Settings settings;
static std::vector<uint32_t> image(lgfx::LGFX_Sprite &lcd) {
    std::vector<uint32_t> result;
    for (int y = 0; y < lcd.height(); ++y)
        for (int x = 0; x < lcd.width(); ++x) result.push_back(lcd.readPixel(x, y));
    return result;
}
static void ppm(lgfx::LGFX_Sprite &lcd, const std::string &path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << lcd.width() << ' ' << lcd.height() << "\n255\n";
    for (auto pixel : image(lcd)) {
        unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
        char rgb[] = {char((r << 3) | (r >> 2)), char((g << 2) | (g >> 4)), char((b << 3) | (b >> 2))};
        out.write(rgb, 3);
    }
}

Model example(int page, bool french) {
    settings.language = french;
    Model m;
    m.page = page; m.language = french;
    m.status = "LIVE"; m.battery = "85%";
    auto metric = [&](int i, const char *label, const char *value, const char *detail = "", uint32_t color = kText) {
        m.metrics[i] = {tr(label), value, detail, color};
    };
    switch (page) {
        case 0:
            m.title = french ? "MINAGE" : "MINING";
            metric(0, "LIVE HASHRATE", "1.005", "MH/s", m.accent);
            metric(1, "SHARES", "10827", "", kGreen); metric(2, "BEST DIFF", "7.82"); metric(3, "TEMP", "52.1 C");
            break;
        case 1:
            m.title = french ? "HORLOGE" : "CLOCK";
            metric(0, "LOCAL TIME", "21:14");
            metric(1, "DATE", french ? "lun 26 oct" : "Mon 26 Oct"); metric(2, "BTC PRICE", "104250", "", m.accent);
            metric(3, "BLOCK", "950128");
            break;
        case 2:
            m.title = "BITCOIN";
            metric(0, "BITCOIN PRICE", "104250", "USD", m.accent); metric(1, "FEES sat/vB", "3/2/1");
            metric(2, "NEXT RETARGET", "+2.34%", "", kGreen); metric(3, "NET HASHRATE", "1.03ZH/s");
            break;
        case 3:
            m.title = french ? "RESEAU" : "NETWORK";
            metric(0, "BLOCK HEIGHT", "950128", "", m.accent);
            metric(1, "NETWORK DIFF", "152.4T"); metric(2, "POOL", "public-pool.io"); metric(3, "HALVING", "99872");
            break;
        case 4:
            m.title = french ? "APPAREIL" : "DEVICE";
            metric(0, "LOCAL IP", "192.168.1.42"); metric(1, "BOARD", "M5StickC Plus2");
            metric(2, "FIRMWARE", "v" FW_VERSION); metric(3, "FREE RAM", "124 KB");
            break;
        case 5:
            m.title = "HASHRATE";
            metric(0, "AVG H/s", "998k", "H/s", m.accent); metric(1, "MIN", "982k"); metric(2, "MAX", "1.02M");
            break;
        case 6:
            m.title = "RECORDS";
            metric(0, "BEST EVER", "12.7", "", m.accent); metric(1, "32-BIT", "21");
            metric(2, "SESSION", "7.82"); metric(3, "RUN TIME", french ? "14j 2h" : "14d 2h");
            break;
        case 7:
            m.title = french ? "BLOCS" : "BLOCKS";
            metric(0, "MINING BLOCK", "950128", "", m.accent);
            metric(1, "950127", "Foundry USA", "2min / 7.82"); metric(2, "950126", "AntPool", "11min / 1.42");
            metric(3, "950125", "ViaBTC", "19min");
            break;
    }
    return m;
}

int main(int argc, char **argv) {
    {
        lgfx::LGFX_Sprite sample;
        sample.setColorDepth(16);
        assert(sample.createSprite(100, 32));
        sample.fillScreen(uint32_t(0xF6DB00));
        sample.setBaseColor(0xF6DB00);
        sample.setFont(ui_font::atMost(24));
        sample.setTextSize(1);
        sample.setTextColor(uint32_t(0x000000));
        sample.drawString("Az 1.05", 1, 1);
        std::set<uint32_t> shades;
        for (auto c : image(sample)) {
            assert((c & 31) == 0); // black on yellow must not acquire blue/black-background fringes
            shades.insert(c);
        }
        assert(shades.size() >= 8); // actual fractional coverage, not a binary bitmap font
        sample.fillScreen(uint32_t(0x000000));
        sample.setBaseColor(0x000000);
        sample.setFont(ui_font::atMost(18, true));
        sample.setTextColor(uint32_t(0xFFFFFF));
        sample.drawString("1.005", 1, 1);
        unsigned inkRows = 0;
        for (int y = 0; y < sample.height(); ++y) {
            bool ink = false;
            for (int x = 0; x < sample.width(); ++x) ink |= sample.readPixel(x, y) != 0;
            if (ink) ++inkRows;
        }
        assert(inkRows >= 15); // numerals use the field's cap height, not unused descender space
    }
    std::string out = argc > 1 ? argv[1] : "";
    const int sizes[][2] = {{240,135}, {320,170}, {320,240}, {160,80}, {128,128}, {80,160}, {135,240}};
    for (const auto &size : sizes) {
        lgfx::LGFX_Sprite lcd, direct;
        lcd.setColorDepth(16); direct.setColorDepth(16);
        assert(lcd.createSprite(size[0], size[1])); assert(direct.createSprite(size[0], size[1]));
        View view(&lcd), fallback(&direct, false);
        std::vector<Point> points;
        for (int i = 0; i < 240; ++i) points.push_back({uint32_t(0xFFFF0000u + i * 10000u), float(1e6 + 18000 * sin(i / 12.0))});
        for (int lang = 0; lang < 2; ++lang) for (int page = 0; page < kPages; ++page) {
            Model m = example(page, lang);
            m.points = points.data(); m.pointCount = points.size(); m.graphRevision = 1;
            view.render(m); fallback.render(m);
            assert(image(lcd) == image(direct));  // strips, fonts, clipping and zero-buffer fallback
            auto first = image(lcd);
            view.render(m); assert(image(lcd) == first);
            // Replace every pixel behind the cache: unchanged data must write
            // nothing at all, including in the graph and metric interiors.
            lcd.fillScreen(uint32_t(0xFF00FF));
            auto marker = image(lcd);
            view.render(m); assert(image(lcd) == marker);
            view.invalidate(); view.render(m);
            assert(image(lcd) == first);
            if (!out.empty()) ppm(lcd, out + "/page" + std::to_string(page) + "_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + (lang ? "_fr" : "_en") + ".ppm");
            m.metrics[0].value = "0"; view.render(m);
            assert(image(lcd) != first); // changed metric renders without changing pages
            m.metrics[0].value = "999999999999999999999999999999999999";
            view.render(m);  // bounded text on every supported shape
            m.accent = 0x3B9CF6; view.render(m);
        }
        Model m = example(5, false);
        m.points = nullptr; m.pointCount = 0; view.invalidate(); view.render(m);
        points[0].rate = 0; m.points = points.data(); m.pointCount = 1; ++m.graphRevision; view.render(m);
        points[1].rate = NAN; points[2].rate = 0; m.pointCount = 3; ++m.graphRevision; view.render(m);
        view.update("Automatic update", FW_VERSION " -> next", 50, "Installing");
        if (!out.empty()) ppm(lcd, out + "/update_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "_en.ppm");
        view.update("Automatic update", FW_VERSION " -> next", 100, "Restarting...");
        view.render(example(0, false)); // failed update restores the mining frame
        view.alert("BLOCK FOUND!", "915432", "Press a button");
        if (!out.empty()) ppm(lcd, out + "/block_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "_en.ppm");
        auto alerted = image(lcd);
        view.alert("BLOCK FOUND!", "915432", "Press a button"); assert(image(lcd) == alerted);
        view.render(example(0, false)); // dismissing restores the mining frame
        view.message("BDLMiner V1", "M5StickC Plus2", "v" FW_VERSION " / hold A: setup");
        if (!out.empty()) ppm(lcd, out + "/splash_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "_en.ppm");
        view.message("WiFi", "Connecting...", "Hold A at boot: setup");
        if (!out.empty()) ppm(lcd, out + "/startup_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "_en.ppm");
        view.portal("WiFi setup", "BDLMinerV1-XXXX", "MineYourCoins", "WIFI:T:WPA;S:BDLMinerV1-XXXX;P:MineYourCoins;;");
        if (!out.empty()) ppm(lcd, out + "/setup_" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "_en.ppm");
        std::cout << size[0] << 'x' << size[1] << ": 8 pages, EN/FR, strip/direct parity, cache, edge cases and update recovery passed\n";
    }
}
