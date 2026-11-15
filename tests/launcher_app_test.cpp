// The desktop launcher's window (launcher/app/), offscreen: one tab per
// settings category from the game's own option table, the rows the game's
// menu would show, and a save that writes only what was changed (and the
// version, for a settings.toml the launcher creates).
// Argument: a scratch folder (its data folder is set through XDG_DATA_HOME).
#include <launcher_window.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTabWidget>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <user/paths.h>
#include <user/settings.h>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;

namespace
{
    std::string Read(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    QStringList Tabs(LauncherWindow& w)
    {
        QStringList out;
        for (int i = 0; i < w.tabs()->count(); i++)
            out << w.tabs()->tabText(i);
        return out;
    }

    bool HasLabel(LauncherWindow& w, const char* text)
    {
        for (QLabel* l : w.findChildren<QLabel*>())
            if (l->text() == QString::fromUtf8(text))
                return true;
        return false;
    }

    int OptionLines(const std::string& text)
    {
        int n = 0;
        bool app = false;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? text.size() : end + 1;
            if (!line.empty() && line[0] == '[')
                app = line == "[app]";
            else if (!app && !line.empty() && line[0] != '#' && line.find(" = ") != std::string::npos)
                n++;
        }
        return n;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    fs::path scratch = argv[1];
    fs::remove_all(scratch);
    fs::create_directories(scratch / "data");
    setenv("XDG_DATA_HOME", (scratch / "data").c_str(), 1);
    setenv("QT_QPA_PLATFORM", "offscreen", 1);
    unsetenv("NFSMW_AA");
    QApplication app(argc, argv);
    const fs::path user = GetUserPath();
    const fs::path toml = user / "settings.toml";

    settings::Load();
    {
        LauncherWindow w(user);
        QStringList tabs = Tabs(w);
        CHECK(tabs.size() >= 5 && tabs[0] == "Display" && tabs[1] == "Graphics" && tabs[2] == "Audio" && tabs[3] == "Input",
            "the game's categories in order: %s", tabs.join(",").toUtf8().constData());
        CHECK(tabs.contains("Advanced"), "Advanced");
        for (const settings::Option& o : settings::Options())
            if (o.hidden && o.type != settings::Type::Key)
                CHECK(!HasLabel(w, o.label), "hidden option %s isn't shown", o.label);
        CHECK(!HasLabel(w, "HD Textures"), "no pack: no HD Textures row");
        CHECK(HasLabel(w, "Upscaling"), "the Upscaling row (FSR)");
        CHECK(!w.findChild<QCheckBox*>("launcher_show_at_start")->text().isEmpty() &&
                  w.findChild<QCheckBox*>("launcher_show_at_start")->isChecked(),
            "the Open-at-start box, ticked by default");

        // Nothing changed: no file.
        CHECK(w.saveIfChanged(), "saved");
        CHECK(!fs::exists(toml), "nothing changed: no settings.toml");

        // One change: that option, and the version for a new file.
        QCheckBox* aa = w.findChild<QCheckBox*>("video.anti_aliasing");
        CHECK(aa != nullptr, "Anti-Aliasing is a check box named by its key");
        if (aa)
            aa->setChecked(!aa->isChecked());
        CHECK(w.saveIfChanged(), "saved");
        std::string text = Read(toml);
        CHECK(OptionLines(text) == 1 && text.find("anti_aliasing = ") != std::string::npos, "only Anti-Aliasing:\n%s", text.c_str());
        CHECK(text.find(std::string("last_version = \"") + SB_VERSION + "\"") != std::string::npos,
            "a file the launcher creates records this version (no \"What's new\" for a new player)");
    }

    // A second session that changes nothing leaves the file byte for byte.
    {
        std::string before = Read(toml);
        settings::Load();
        LauncherWindow w(user);
        CHECK(w.saveIfChanged() && Read(toml) == before, "unchanged");
    }

    // An existing file's version is never touched.
    {
        std::ofstream(toml, std::ios::trunc) << "[video]\nanti_aliasing = false\n\n[app]\nlast_version = \"0.2.4\"\n";
        settings::Load();
        LauncherWindow w(user);
        QComboBox* scaling = w.findChild<QComboBox*>("video.scaling");
        CHECK(scaling != nullptr, "Upscaling is a list named by its key");
        if (scaling)
            scaling->setCurrentIndex((scaling->currentIndex() + 1) % scaling->count());
        CHECK(w.saveIfChanged(), "saved");
        std::string text = Read(toml);
        CHECK(text.find("last_version = \"0.2.4\"") != std::string::npos, "the game's record stays (its update note still shows)");
        CHECK(OptionLines(text) == 2, "Anti-Aliasing and Upscaling:\n%s", text.c_str());
    }

    // HD Textures appears with a pack.
    fs::create_directories(user / "texture-pack");
    std::ofstream(user / "texture-pack" / "pack.json") << "{}";
    {
        settings::Load();
        LauncherWindow w(user);
        CHECK(HasLabel(w, "HD Textures"), "a pack: the HD Textures row");
    }

    if (g_failures)
        printf("launcher_app_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_app_test: all passed\n");
    return g_failures ? 1 : 0;
}
