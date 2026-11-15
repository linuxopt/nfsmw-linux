// Tests for settings::SaveTouched (runtime/user/settings.cpp), the desktop
// launcher's save: only the options the file already holds plus the ones
// changed since, so a launcher session never pins the game's defaults (a
// PAL disc's language, a GPU's own defaults) into settings.toml. Also the
// launcher's own hidden option, [launcher] show_at_start.
// Argument: a scratch folder (its files are written and read).
#include <user/settings.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using settings::Id;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;

namespace
{
    void WriteFile(const fs::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << text;
    }

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    // The option keys ("name = value" lines) outside the [app] table.
    std::vector<std::string> OptionKeys(const std::string& text)
    {
        std::vector<std::string> keys;
        bool app = false;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '[')
            {
                app = line == "[app]";
                continue;
            }
            size_t eq = line.find(" = ");
            if (!app && eq != std::string::npos)
                keys.push_back(line.substr(0, eq));
        }
        return keys;
    }

    double OtherEntry(Id id)
    {
        const settings::Option& o = settings::Info(id);
        for (const settings::EnumEntry& e : o.entries)
            if (double(e.value) != o.defaultValue)
                return e.value;
        return o.defaultValue;
    }

    void NoFileNothingChanged(const fs::path& dir)
    {
        fs::path p = dir / "a.toml";
        fs::remove(p);
        settings::Load(p);
        CHECK(settings::SaveTouched(), "nothing to save is a success");
        CHECK(!fs::exists(p), "no file is created when nothing changed");
    }

    void NoFileOneChange(const fs::path& dir)
    {
        fs::path p = dir / "b.toml";
        fs::remove(p);
        settings::Load(p);
        double v = OtherEntry(Id::Scaling);
        settings::Set(Id::Scaling, v);
        CHECK(settings::SaveTouched(), "saved");
        std::string text = ReadFile(p);
        std::vector<std::string> keys = OptionKeys(text);
        CHECK(keys.size() == 1 && keys[0] == "scaling", "only the changed option is written (%zu keys)\n%s", keys.size(), text.c_str());
        settings::Load(p);
        CHECK(settings::GetStoredValue(Id::Scaling) == v, "and it loads back");
    }

    void MinimalFileUntouched(const fs::path& dir)
    {
        fs::path p = dir / "c.toml";
        const std::string text = "[video]\nanti_aliasing = false\n";
        WriteFile(p, text);
        settings::Load(p);
        CHECK(settings::SaveTouched(), "nothing to save is a success");
        CHECK(ReadFile(p) == text, "the file is left byte for byte");
    }

    void MinimalFilePlusChange(const fs::path& dir)
    {
        fs::path p = dir / "d.toml";
        WriteFile(p, "[video]\nanti_aliasing = false\n");
        settings::Load(p);
        settings::Set(Id::Sharpening, 0.5);
        CHECK(settings::SaveTouched(), "saved");
        std::string text = ReadFile(p);
        std::vector<std::string> keys = OptionKeys(text);
        CHECK(keys.size() == 2, "the file's option and the changed one (%zu keys)\n%s", keys.size(), text.c_str());
        settings::Load(p);
        CHECK(!settings::GetBool(Id::AntiAliasing) && settings::GetStoredValue(Id::Sharpening) == 0.5, "both load back");
        // A second change after the save: the file's two plus the new one.
        settings::Set(Id::Scaling, OtherEntry(Id::Scaling));
        CHECK(settings::SaveTouched(), "saved again");
        CHECK(OptionKeys(ReadFile(p)).size() == 3, "three options now");
    }

    void BadFileKept(const fs::path& dir)
    {
        fs::path p = dir / "e.toml";
        const std::string text = "[video\nthis isn't toml\n";
        WriteFile(p, text);
        settings::Load(p);
        settings::Set(Id::Sharpening, 0.25);
        CHECK(!settings::SaveTouched(), "a file that didn't load isn't overwritten");
        CHECK(ReadFile(p) == text, "the player's file stays as it was");
    }

    void LauncherOption(const fs::path& dir)
    {
        const settings::Option& o = settings::Info(Id::LauncherShowAtStart);
        CHECK(o.hidden && o.type == settings::Type::Bool && o.defaultValue == 1, "hidden, Bool, on by default");
        CHECK(std::string(o.table) == "launcher" && std::string(o.key) == "show_at_start", "[launcher] show_at_start");
        CHECK(settings::Find("launcher", "show_at_start") == &o, "found by its key");
        fs::path p = dir / "f.toml";
        WriteFile(p, "[launcher]\nshow_at_start = false\n");
        settings::Load(p);
        CHECK(!settings::GetBool(Id::LauncherShowAtStart), "the file turns it off");
        settings::Set(Id::Sharpening, 0.75);
        CHECK(settings::Save(), "a full save");
        settings::Load(p);
        CHECK(!settings::GetBool(Id::LauncherShowAtStart), "and a full save keeps it");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: settings_touched_test <scratch folder>\n");
        return 2;
    }
    fs::path dir = argv[1];
    fs::create_directories(dir);
    NoFileNothingChanged(dir);
    NoFileOneChange(dir);
    MinimalFileUntouched(dir);
    MinimalFilePlusChange(dir);
    BadFileKept(dir);
    LauncherOption(dir);
    if (g_failures)
        printf("settings_touched_test: %d failure(s)\n", g_failures);
    else
        printf("settings_touched_test: all passed\n");
    return g_failures ? 1 : 0;
}
