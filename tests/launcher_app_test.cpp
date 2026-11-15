// The desktop launcher's window (launcher/app/), offscreen: one tab per
// settings category from the game's own option table, the rows the game's
// menu would show, and a save that writes only what was changed (and the
// version, for a settings.toml the launcher creates).
// Argument: a scratch folder (its data folder is set through XDG_DATA_HOME).
#include <launcher_window.h>
#include <theme.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QWheelEvent>
#include <QTabWidget>

#include "launcher_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <user/paths.h>
#include <user/settings.h>
#include <game/language.h>
#include <install/manifest.h>

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

    size_t Count(const fs::path& dir)
    {
        size_t n = 0;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            n++;
        return n;
    }

    void fixture_profile(const fs::path& user)
    {
        fixture::Profile(user, "save", "TJ", "career");
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

    // Dark, whatever the desktop's theme (offscreen has none: Qt's light default).
    CHECK(app.palette().color(QPalette::Window).lightness() > 128, "the test starts from Qt's light default");
    launcher::ApplyDarkTheme(app);
    {
        const QPalette p = app.palette();
        CHECK(p.color(QPalette::Window).lightness() < 60, "a dark window");
        CHECK(p.color(QPalette::Base).lightness() < 60, "dark fields");
        CHECK(p.color(QPalette::Button).lightness() < 70, "dark buttons");
        CHECK(p.color(QPalette::WindowText).lightness() > 200 && p.color(QPalette::Text).lightness() > 200 &&
                  p.color(QPalette::ButtonText).lightness() > 200,
            "light text");
        CHECK(p.color(QPalette::Disabled, QPalette::ButtonText).lightness() < p.color(QPalette::ButtonText).lightness(),
            "disabled text dimmer");
        CHECK(p.color(QPalette::Mid).lightness() > 110, "the env note's colour stays readable");
        settings::Load();
        LauncherWindow w(user);
        CHECK(w.palette().color(QPalette::Window) == p.color(QPalette::Window), "the window takes it");
    }
    const fs::path toml = user / "settings.toml";

    settings::Load();
    {
        LauncherWindow w(user);
        QStringList tabs = Tabs(w);
        CHECK(tabs.size() >= 5 && tabs[0] == "Display" && tabs[1] == "Graphics" && tabs[2] == "Audio" && tabs[3] == "Input",
            "the game's categories in order: %s", tabs.join(",").toUtf8().constData());
        CHECK(tabs.contains("Advanced"), "Advanced");
        for (const settings::Option& o : settings::Options())
        {
            // (A hidden wheel button and a Controls row can share a label.)
            bool alsoShown = false;
            for (const settings::Option& other : settings::Options())
                alsoShown |= !other.hidden && std::string(other.label) == o.label;
            if (o.hidden && !alsoShown)
                CHECK(!HasLabel(w, o.label), "hidden option %s isn't shown", o.label);
        }
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

    // The Controls tab: 24 controls, two keys each, at the Controls
    // category's place; a key press binds by its position.
    {
        settings::Load();
        LauncherWindow w(user);
        w.messageHook = [](const QString& m) { printf("  (message: %s)\n", m.toUtf8().constData()); };
        QStringList tabs = Tabs(w);
        CHECK(tabs.indexOf("Controls") == tabs.indexOf("Wheel") + 1 || (tabs.indexOf("Wheel") < 0 && tabs.indexOf("Controls") == 4),
            "Controls in the game's order: %s", tabs.join(",").toUtf8().constData());
        CHECK(tabs.last() == "Saves", "Saves last");
        int keyButtons = 0;
        for (QPushButton* b : w.findChildren<QPushButton*>())
            keyButtons += b->objectName().startsWith("keys.");
        CHECK(keyButtons == 48, "24 controls x 2 keys (%d)", keyButtons);

        QPushButton* rt = w.findChild<QPushButton*>("keys.right_trigger");
        CHECK(rt != nullptr, "Right Trigger's first key");
        if (rt)
        {
            auto press = [&](int qtKey, quint32 native, Qt::KeyboardModifiers mods = Qt::NoModifier) {
                QKeyEvent e(QEvent::KeyPress, qtKey, mods, native, 0, 0, QString());
                QApplication::sendEvent(rt, &e);
            };
            settings::Set(settings::Id::KeyRightTrigger, 4);  // a
            rt->click();
            press(Qt::Key_Escape, 9);
            CHECK(settings::GetStoredValue(settings::Id::KeyRightTrigger) == 4, "Esc: no new key");
            rt->click();
            press(Qt::Key_Super_L, 133);
            CHECK(settings::GetStoredValue(settings::Id::KeyRightTrigger) == 4, "Super can't be bound");
            CHECK(w.findChild<QLabel*>("help")->text().contains("can't be used"), "and the help line says so");
            press(Qt::Key_W, 25);
            CHECK(settings::GetStoredValue(settings::Id::KeyRightTrigger) == 26, "the wait went on: W bound (26)");
            CHECK(rt->text() == "W", "the button shows it: %s", rt->text().toUtf8().constData());
            press(Qt::Key_D, 40);
            CHECK(settings::GetStoredValue(settings::Id::KeyRightTrigger) == 26, "no wait: a key press does nothing");
            rt->click();
            press(Qt::Key_Shift, 50);
            CHECK(settings::GetStoredValue(settings::Id::KeyRightTrigger) == 225, "Left Shift (225)");
            CHECK(rt->text() == "Left Shift", "named: %s", rt->text().toUtf8().constData());
        }
    }

    // The Saves tab: back up, then delete (backed up first).
    {
        fixture_profile(user);
        settings::Load();
        LauncherWindow w(user);
        w.confirmHook = [](const QString&) { return true; };
        w.messageHook = [](const QString& m) { printf("  (message: %s)\n", m.toUtf8().constData()); };
        QListWidget* list = w.findChild<QListWidget*>("saves_list");
        CHECK(list && list->count() == 1 && list->item(0)->text().contains("TJ"), "TJ listed");
        if (list && list->count() == 1)
        {
            list->setCurrentRow(0);
            w.findChild<QPushButton*>("backup")->click();
            CHECK(Count(user / "save-backups") == 1, "one backup (%zu)", Count(user / "save-backups"));
            w.findChild<QPushButton*>("delete")->click();
            CHECK(!fs::exists(user / "save" / "TJ") && !fs::exists(user / "save" / "TJ.xcontent"), "deleted");
            CHECK(Count(user / "save-backups") == 2, "after a backup (%zu)", Count(user / "save-backups"));
            CHECK(list->count() == 0, "the list follows");
            CHECK(!w.findChild<QLabel*>("saves_empty")->isHidden(), "and says there are none");
        }
    }

    // The mouse wheel over a list or a slider that wasn't clicked scrolls the
    // page and changes nothing (a page scrolled past it would otherwise
    // change settings under the pointer, saved on Play).
    {
        settings::Load();
        LauncherWindow w(user);
        w.show();
        w.tabs()->setCurrentIndex(1);  // Graphics, where both are
        QApplication::processEvents();
        QComboBox* scaling = w.findChild<QComboBox*>("video.scaling");
        QSlider* sharpening = w.findChild<QSlider*>("video.sharpening");
        CHECK(scaling && sharpening, "the Upscaling list and the Sharpening slider");
        if (scaling && sharpening)
        {
            const int index = scaling->currentIndex(), value = sharpening->value();
            for (QWidget* target : { static_cast<QWidget*>(scaling), static_cast<QWidget*>(sharpening) })
                for (int dy : { -120, 120 })
                {
                    QWheelEvent e(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(target, &e);
                    CHECK(scaling->currentIndex() == index && sharpening->value() == value, "one wheel step (%d) moved nothing", dy);
                }
            CHECK(scaling->currentIndex() == index, "the list didn't move");
            CHECK(sharpening->value() == value, "the slider didn't move");
            CHECK(!settings::Dirty(), "nothing to save");
            CHECK(scaling->focusPolicy() == Qt::StrongFocus && sharpening->focusPolicy() == Qt::StrongFocus,
                "the wheel doesn't take focus either");
        }
    }

    // A PAL disc's language is the Language row's default, as in the game:
    // shown as such, and English (the table's default) saved when chosen.
    {
        const install::Manifest* palEs = nullptr;
        for (const install::Manifest* m : install::GameManifests())
            if (m->region == "pal-es")
                palEs = m;
        CHECK(palEs != nullptr, "the PAL ES manifest");
        if (palEs)
        {
            fs::create_directories(user / "game");
            std::ofstream(user / "game" / "default.xex").close();
            fs::resize_file(user / "game" / "default.xex", palEs->Xex().size);
            std::ofstream(toml, std::ios::trunc) << "[video]\nanti_aliasing = false\n";
            launcher::ApplyReleaseDefaults(user);
            settings::Load();
            LauncherWindow w(user);
            QComboBox* language = w.findChild<QComboBox*>("game.language");
            CHECK(language != nullptr, "the Language list");
            const int32_t spanish = game::language::DiscLanguage("pal-es");
            if (language)
            {
                CHECK(language->currentData().toInt() == spanish, "shows the disc's language (%d), not English",
                    language->currentData().toInt());
                language->setCurrentIndex(language->findData(int(game::language::kEnglish)));
                CHECK(w.saveIfChanged(), "saved");
                std::string text = Read(toml);
                CHECK(text.find("language = \"english\"") != std::string::npos, "English is written:\n%s", text.c_str());
            }
        }
    }

    if (g_failures)
        printf("launcher_app_test: %d failure(s)\n", g_failures);
    else
        printf("launcher_app_test: all passed\n");
    return g_failures ? 1 : 0;
}
