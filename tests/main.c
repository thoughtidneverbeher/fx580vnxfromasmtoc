#include "harness.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    harness_load_rom(argc > 1 ? argv[1] : "resource/rom.bin");

    test_string();
    test_mem();
    test_display();
    test_font();
    test_print();
    test_keyboard();
    test_emu();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
