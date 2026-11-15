// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The renderer's use of the texture pack (gpu/renderer.cpp, read as text,
// as depth_only_test does): replacements only for eligible textures, under
// HD Textures' Auto and its budget, BC7 images shared by key, swapped in by
// invalidating the lookup cache, a reload whose key changed dropping the old
// replacement (the city streams new art into reused memory), and evicted
// images destroyed only once the GPU is done with them.
// Argument: runtime/gpu/renderer.cpp.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int s_failures = 0;
#define CHECK(cond, what)                                              \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            s_failures++;                                              \
            fprintf(stderr, "FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #cond, what); \
        }                                                              \
    } while (0)

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    auto has = [&](const char* s) { return src.find(s) != std::string::npos; };

    CHECK(has("texpack::AutoTexpack("), "HD Textures' Auto decides the cap");
    CHECK(has("texpack::TexpackBudgetMb("), "the memory budget");
    CHECK(has("NFSMW_TEXPACK_CAP") && has("NFSMW_TEXPACK_BUDGET_MB") && has("\"NFSMW_TEXPACK\""), "the test overrides");
    CHECK(has("VK_FORMAT_BC7_UNORM_BLOCK"), "replacements are BC7 images");
    CHECK(has("TexpackView(") && has("TexpackPump("), "looked up and uploaded");
    CHECK(has("texpack::Eligible(in)"), "only eligible textures");
    CHECK(has("[texpack] rehash"), "a reload whose key changed is noticed");
    CHECK(has("t.packKey != key"), "and compared before a replacement is reused");
    CHECK(has("completed >= dead.submission") && !has("s_completedSubmission >= dead.submission"),
        "evicted images wait for the GPU, read under its lock");
    // Final review fixes.
    CHECK(has("fused->packKey = 0"), "a resolve into a texture drops its replacement (it now shows rendered content)");
    CHECK(has("s_tp.pumpFrame != s_frame") && has("kTexpackUploadBytesPerFrame"), "uploads once a frame, within a byte budget");
    CHECK(has("TexpackRefreshBudget("), "the budget follows the game's real memory use");
    CHECK(has("if (fallible && allocated != VK_SUCCESS)"), "a replacement whose memory can't be had is skipped, not fatal");
    CHECK(has("budgetMb == 0"), "no budget: HD textures off");
    CHECK(has("kTexpackIdleFrames"), "only replacements idle a while are evicted");
    CHECK(has("s_tp.resident.count(key)"), "a key already resident isn't uploaded twice");
    CHECK(has("TexpackTouch("), "lookup cache hits keep replacements recently used");

    if (s_failures)
        fprintf(stderr, "%d failure(s)\n", s_failures);
    else
        printf("texpack_renderer_test: all passed\n");
    return s_failures ? 1 : 0;
}
