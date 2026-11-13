// Disposable-database lifecycle checks through the game's net_client/Rust SDK.
#include "check_runtime.h"
#include "game/content_files.h"
#include <cstdio>
#include <filesystem>
namespace {
bool loopback_endpoint(const std::string& server) {
    for (const std::string prefix : {"http://127.0.0.1:", "http://localhost:"}) {
        if (server.rfind(prefix, 0) != 0) { continue; }
        const auto port = server.substr(prefix.size());
        if (port.empty() || port.size() > 5) { return false; }
        unsigned number = 0;
        for (const char digit : port) {
            if (digit < '0' || digit > '9') { return false; }
            number = number * 10 + static_cast<unsigned>(digit - '0');
        }
        return number > 0 && number <= 65535;
    }
    return false;
}
}
int main(const int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3 || argc > 4) { std::fprintf(stderr, "usage: golfpp-online-check <loopback server URI> <disposable database> [course id]\n"); return 2; }
    const std::string server = argv[1], database = argv[2];
    if (!loopback_endpoint(server) || database.rfind("cleanup2-check-", 0) != 0) {
        std::fprintf(stderr, "requires loopback server and fresh database named cleanup2-check-<unique suffix>\n"); return 2;
    }
    auto content = load_game_content(GOLFPP_ASSETS_DIR);
    const auto text = read_text_file(std::filesystem::path(GOLFPP_ASSETS_DIR) / online_config_path);
    auto config = text ? parse_online_config_from_text(*text) : std::nullopt;
    if (!content.content || !config) { std::fprintf(stderr, "cannot load game content or online configuration\n"); return 2; }
    config->auth_client_id = database; // isolated credential namespace, even for guest lifecycle checks
    live_check run(std::move(*content.content)); run.course = argc == 4 ? argv[3] : "jelling_par_3";
    const bool passed = run.connect(with_overrides(*config, server, database, true)) && check_booking(run) && check_practice(run) && check_social(run) && check_round(run);
    std::printf("%s\n", passed ? "all live lifecycle checks passed" : "live lifecycle check failed"); return passed ? 0 : 1;
}
