// SpeedBreaker desktop launcher. GPL-3.0-or-later (see COPYING).
#include "online.h"
#include "durable.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace launcher::online
{
    namespace
    {
        std::filesystem::path FilePath(const std::filesystem::path& userDir) { return userDir / "online.toml"; }

        // A TOML basic string.
        std::string Quote(const std::string& s)
        {
            std::string out = "\"";
            for (char c : s)
            {
                if (c == '"' || c == '\\')
                    out += '\\';
                out += c;
            }
            return out + "\"";
        }
    }

    bool Load(const std::filesystem::path& userDir, Config& out, std::string& error)
    {
        out = Config{};
        std::filesystem::path path = FilePath(userDir);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return true;
        try
        {
            toml::table t = toml::parse_file(path.string());
            out.enabled = t["enabled"].value_or(true);
            out.name = t["name"].value_or(std::string());
            out.server = t["server"].value_or(std::string(kDefaultServer));
            out.id = t["id"].value_or(std::string());
            return true;
        }
        catch (const toml::parse_error& e)
        {
            std::ostringstream text;
            text << path.string() << ": " << e.description() << " (line " << e.source().begin.line << ")";
            error = text.str();
            return false;
        }
    }

    bool Save(const std::filesystem::path& userDir, const Config& c, std::string& error)
    {
        std::ostringstream text;
        text << "# SpeedBreaker online play (the launcher's Online page writes this).\n"
             << "# enabled: online play on or off; name: what other players see (up to 15\n"
             << "# characters); server: host:port of the online server. Keep id as it is:\n"
             << "# it is how the server tells you apart.\n"
             << "enabled = " << (c.enabled ? "true" : "false") << "\n"
             << "name = " << Quote(c.name) << "\n"
             << "server = " << Quote(c.server) << "\n";
        if (!c.id.empty())
            text << "id = " << Quote(c.id) << "\n";
        std::filesystem::path path = FilePath(userDir), partial = path;
        partial += ".partial";
        {
            std::ofstream f(partial, std::ios::binary | std::ios::trunc);
            f << text.str();
            f.close();
            if (!f)
            {
                error = "can't write " + partial.string();
                return false;
            }
        }
        durable::SyncFile(partial);
        std::error_code ec;
        std::filesystem::rename(partial, path, ec);
        if (ec)
        {
            std::filesystem::remove(partial, ec);
            error = "can't save " + path.string();
            return false;
        }
        durable::SyncDir(userDir);
        return true;
    }

    std::string NameProblem(const std::string& name)
    {
        if (name.empty())
            return "Pick a name for online play.";
        if (name.size() > 15)
            return "The name is too long: 15 characters at most.";
        for (unsigned char ch : name)
            if (ch < 0x20 || ch >= 0x7F)
                return "The name can only have letters, digits, spaces and punctuation from a US keyboard.";
        if (name.front() == ' ' || name.back() == ' ')
            return "The name can't start or end with a space.";
        return {};
    }

    std::string ServerProblem(const std::string& server)
    {
        const std::string example = " (for example " + std::string(kDefaultServer) + ").";
        size_t colon = server.rfind(':');
        if (server.empty() || colon == std::string::npos || colon + 1 >= server.size())
            return "The server needs a host and a port" + example;
        std::string host = server.substr(0, colon), port = server.substr(colon + 1);
        if (host.size() > 2 && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        if (host.empty())
            return "The server needs a host and a port" + example;
        for (unsigned char ch : host)
            if (ch <= 0x20 || ch >= 0x7F)
                return "The server's host can't have spaces or other unusual characters.";
        if (port.size() > 5)
            return "The server's port must be a number from 1 to 65535.";
        for (char ch : port)
            if (ch < '0' || ch > '9')
                return "The server's port must be a number from 1 to 65535.";
        int n = std::stoi(port);
        if (n < 1 || n > 65535)
            return "The server's port must be a number from 1 to 65535.";
        return {};
    }
}
