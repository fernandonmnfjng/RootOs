#include <rootos/rootapp.h>

int root_main(
    const RootApi* api,
    int argc,
    const char** argv
)
{
    if (
        api == 0
        ||
        api->abi_version != ROOT_APP_ABI_VERSION
    )
    {
        return 100;
    }

    api->print("Hello from a real RootOS ELF application!\n");
    api->print("RootOS version: ");
    api->print(api->os_version());
    api->putchar('\n');

    api->print("Arguments: ");

    for (int i = 0; i < argc; i++)
    {
        if (i != 0)
        {
            api->print(" | ");
        }

        api->print(argv[i]);
    }

    api->putchar('\n');

    return 0;
}
