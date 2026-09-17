#include "openxr_host.h"
#if defined(_WIN32)
#include <windows.h>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** wideArgv)
{
    std::vector<std::string> args(argc);
    std::vector<char*> argv(argc);
    for (int i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wideArgv[i], -1, nullptr, 0, nullptr, nullptr);
        args[i].resize(size);
        WideCharToMultiByte(CP_UTF8, 0, wideArgv[i], -1, args[i].data(), size, nullptr, nullptr);
        argv[i] = args[i].data();
    }
    axrb::host::OpenXrHost host;
    return host.run(argc, argv.data());
}
#else

int main(int argc, char** argv)
{
    axrb::host::OpenXrHost host;
    return host.run(argc, argv);
}
#endif
