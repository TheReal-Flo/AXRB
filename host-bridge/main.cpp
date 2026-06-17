#include "openxr_host.h"

int main(int argc, char** argv)
{
    axrb::host::OpenXrHost host;
    return host.run(argc, argv);
}
