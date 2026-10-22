/* t_string.c - differential tests for the string cluster. */
#include "harness.h"

#include <string.h>

#include "../src/runtime.h"

static void test_strlen(void)
{
    static const uint16_t lens[] = {0, 1, 2, 3, 15, 16, 63, 64, 255, 256, 400};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        for (int rep = 0; rep < 3; rep++) {
            random_image((uint32_t)(t * 7 + rep + 1));
            uint16_t p = 0xD200;
            uint16_t n = lens[t];
            for (uint16_t i = 0; i < n; i++)
                image[p + i] = (uint8_t)(1 + (rnd() % 255));
            image[p + n] = 0;
            Args a = {.er = {p}, .nstk = 0};
            uint16_t r = run_rom(0x29A2C, &a, "strlen_n");
            begin_c();
            uint16_t c = strlen_n(after_c, p);
            check_ret("strlen_n", r, c);
            check_mem("strlen_n", RAM_BASE, CMP_END);
        }
    }
}

static void test_byte_strlen(void)
{
    static const uint16_t lens[] = {0, 1, 2, 5, 100, 255, 256, 300};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        random_image((uint32_t)(t + 100));
        uint16_t p = 0xD300;
        uint16_t n = lens[t];
        for (uint16_t i = 0; i < n; i++)
            image[p + i] = (uint8_t)(1 + (rnd() % 255));
        image[p + n] = 0;
        Args a = {.er = {p}, .nstk = 0};
        uint16_t r = run_rom(0x2AB26, &a, "byte_strlen_n");
        begin_c();
        uint8_t c = byte_strlen_n(after_c, p);
        check_ret("byte_strlen_n", (uint16_t)(r & 0xFF), c);
        check_mem("byte_strlen_n", RAM_BASE, CMP_END);
    }
}

static void test_strcpy(void)
{
    static const uint16_t lens[] = {0, 1, 2, 9, 64, 200, 500};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        random_image((uint32_t)(t + 200));
        uint16_t dst = 0xD200, src = 0xD700;
        uint16_t n = lens[t];
        for (uint16_t i = 0; i < n; i++)
            image[src + i] = (uint8_t)(1 + (rnd() % 255));
        image[src + n] = 0;
        Args a = {.er = {dst, src}, .nstk = 0};
        uint16_t r = run_rom(0x0E5C8, &a, "strcpy_nn");
        begin_c();
        uint16_t c = strcpy_nn(after_c, dst, src);
        check_ret("strcpy_nn", r, c);
        check_mem("strcpy_nn", RAM_BASE, CMP_END);
    }
}

static void test_strcat(void)
{
    static const uint16_t plens[] = {0, 1, 5, 40, 120};
    static const uint16_t slens[] = {0, 1, 3, 30, 90};
    for (size_t i = 0; i < sizeof(plens) / sizeof(plens[0]); i++) {
        for (size_t j = 0; j < sizeof(slens) / sizeof(slens[0]); j++) {
            random_image((uint32_t)(i * 10 + j + 300));
            uint16_t dst = 0xD200, src = 0xD700;
            for (uint16_t k = 0; k < plens[i]; k++)
                image[dst + k] = (uint8_t)(1 + (rnd() % 255));
            image[dst + plens[i]] = 0;
            for (uint16_t k = 0; k < slens[j]; k++)
                image[src + k] = (uint8_t)(1 + (rnd() % 255));
            image[src + slens[j]] = 0;
            Args a = {.er = {dst, src}, .nstk = 0};
            uint16_t r = run_rom(0x2EDA0, &a, "strcat_nn");
            begin_c();
            uint16_t c = strcat_nn(after_c, dst, src);
            check_ret("strcat_nn", r, c);
            check_mem("strcat_nn", RAM_BASE, CMP_END);
        }
    }
}

static void test_smart(void)
{
    random_image(401);
    Args a0 = {.nstk = 0};
    uint16_t r = run_rom(0x203B8, &a0, "smart_strlen_n(NULL)");
    begin_c();
    check_ret("smart_strlen_n(NULL)", r, smart_strlen_n(after_c, 0));
    check_mem("smart_strlen_n(NULL)", RAM_BASE, CMP_END);

    struct { uint16_t dst, src; const char *name; } cases[] = {
        {0x0000, 0xD700, "smart_strcpy(dst=NULL)"},
        {0xD200, 0x0000, "smart_strcpy(src=NULL)"},
        {0x0000, 0x0000, "smart_strcpy(both NULL)"},
        {0xD200, 0xD700, "smart_strcpy(normal)"},
    };
    for (int i = 0; i < 4; i++) {
        random_image((uint32_t)(402 + i));
        memcpy(image + 0xD700, "hello", 6);
        memcpy(image + 0xD200, "XXXXXXXX", 8);
        Args a = {.er = {cases[i].dst, cases[i].src}, .nstk = 0};
        uint16_t rr = run_rom(0x203C2, &a, cases[i].name);
        begin_c();
        uint16_t c = smart_strcpy_nn(after_c, cases[i].dst, cases[i].src);
        check_ret(cases[i].name, rr, c);
        check_mem(cases[i].name, RAM_BASE, CMP_END);
    }

    for (int i = 0; i < 4; i++) {
        random_image((uint32_t)(410 + i));
        memcpy(image + 0xD700, "world", 6);
        memcpy(image + 0xD200, "abc", 4);
        Args a = {.er = {cases[i].dst, cases[i].src}, .nstk = 0};
        uint16_t rr = run_rom(0x203D6, &a, cases[i].name);
        begin_c();
        uint16_t c = smart_strcat_nn(after_c, cases[i].dst, cases[i].src);
        check_ret(cases[i].name, rr, c);
        check_mem(cases[i].name, RAM_BASE, CMP_END);
    }
}

void test_string(void)
{
    test_strlen();
    test_byte_strlen();
    test_strcpy();
    test_strcat();
    test_smart();
}
