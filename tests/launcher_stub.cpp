// A stand-in for speedbreaker-launcher in launcher_apprun_test.sh: says it
// ran (with its arguments and whether AppRun gave it Qt's plugin path), then
// exits with $STUB_EXIT (0 when unset). A real program, so AppRun can run it
// through the dynamic loader as it runs the real one.
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    printf("LAUNCHER");
    for (int i = 1; i < argc; i++)
        printf(" %s", argv[i]);
    const char* qt = std::getenv("QT_PLUGIN_PATH");
    const char* own = std::getenv("SB_QT_PLUGINS");
    printf("\nLAUNCHERENV QT=%s SB=%s\n", qt ? "set" : "unset", own ? "set" : "unset");
    fflush(stdout);
    const char* code = std::getenv("STUB_EXIT");
    return code ? std::atoi(code) : 0;
}
