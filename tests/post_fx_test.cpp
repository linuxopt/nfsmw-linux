// Haze (game/post_fx.h): the tone curve's black point scaled by the setting.
#include <game/post_fx.h>

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main()
{
    using game::post_fx::HazeBlack;
    CHECK(HazeBlack(0.15f, 100) == 0.15f, "100%%: the game's own");
    CHECK(HazeBlack(0.15f, 0) == 0.0f, "0%%: black stays black");
    CHECK(HazeBlack(0.15f, 50) > 0.0749f && HazeBlack(0.15f, 50) < 0.0751f, "50%%: half (%f)", HazeBlack(0.15f, 50));
    CHECK(HazeBlack(0.15f, 150) == 0.15f && HazeBlack(0.15f, -10) == 0.0f, "clamped");
    CHECK(HazeBlack(0.0f, 30) == 0.0f, "a curve without a lift keeps none");
    static_assert(game::post_fx::kCurveBlack == 20);
    if (g_failures)
        printf("post_fx_test: %d failure(s)\n", g_failures);
    else
        printf("post_fx_test: all passed\n");
    return g_failures ? 1 : 0;
}
