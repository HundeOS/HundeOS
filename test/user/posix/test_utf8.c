#include "unity.h"

#include <wchar.h>
#include <wctype.h>

#define btowc hunde_btowc
#define iswalnum hunde_iswalnum
#define iswalpha hunde_iswalpha
#define iswblank hunde_iswblank
#define iswcntrl hunde_iswcntrl
#define iswctype hunde_iswctype
#define iswdigit hunde_iswdigit
#define iswgraph hunde_iswgraph
#define iswlower hunde_iswlower
#define iswprint hunde_iswprint
#define iswpunct hunde_iswpunct
#define iswspace hunde_iswspace
#define iswupper hunde_iswupper
#define iswxdigit hunde_iswxdigit
#define mblen hunde_mblen
#define mbrlen hunde_mbrlen
#define mbrtowc hunde_mbrtowc
#define mbstowcs hunde_mbstowcs
#define mbsrtowcs hunde_mbsrtowcs
#define mbsinit hunde_mbsinit
#define mbtowc hunde_mbtowc
#define towlower hunde_towlower
#define towupper hunde_towupper
#define wcrtomb hunde_wcrtomb
#define wcschr hunde_wcschr
#define wcscoll hunde_wcscoll
#define wcscmp hunde_wcscmp
#define wcscpy hunde_wcscpy
#define wcslen hunde_wcslen
#define wcsncmp hunde_wcsncmp
#define wcsrtombs hunde_wcsrtombs
#define wcstombs hunde_wcstombs
#define wcswidth hunde_wcswidth
#define wctob hunde_wctob
#define wctomb hunde_wctomb
#define wmemchr hunde_wmemchr
#define wctype hunde_wctype
#define wcwidth hunde_wcwidth

#include "../../../user/lib/newlib/locale.c"
#include "../../../user/lib/newlib/wide_char.c"

void setUp(void)
{
    setlocale(LC_ALL, "C.UTF-8");
}

void tearDown(void)
{
}

void test_locale_selects_c_and_utf8_modes(void)
{
    TEST_ASSERT_EQUAL_STRING("C", setlocale(LC_ALL, "POSIX"));
    TEST_ASSERT_EQUAL_UINT(1, __locale_mb_cur_max());
    TEST_ASSERT_EQUAL_STRING("C.UTF-8", setlocale(LC_CTYPE, "C.UTF-8"));
    TEST_ASSERT_EQUAL_UINT(4, __locale_mb_cur_max());
    TEST_ASSERT_EQUAL_STRING("C.UTF-8", setlocale(LC_CTYPE, NULL));
}

void test_locale_rejects_unknown_names(void)
{
    errno = 0;
    TEST_ASSERT_NULL(setlocale(LC_ALL, "en_AU.UTF-8"));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_mbrtowc_decodes_utf8_codepoints(void)
{
    wchar_t character = 0;
    mbstate_t state = {0};

    TEST_ASSERT_EQUAL_UINT(3, mbrtowc(&character, "\xe2\x82\xac", 3, &state));
    TEST_ASSERT_EQUAL_HEX32(0x20ac, character);
    TEST_ASSERT_EQUAL_UINT(4, mbrtowc(&character, "\xf0\x9f\x98\x80", 4, &state));
    TEST_ASSERT_EQUAL_HEX32(0x1f600, character);
}

void test_mbrtowc_continues_a_split_sequence(void)
{
    wchar_t character = 0;
    mbstate_t state = {0};

    TEST_ASSERT_EQUAL_UINT((size_t)-2, mbrtowc(&character, "\xe2\x82", 2, &state));
    TEST_ASSERT_EQUAL_UINT(1, mbrtowc(&character, "\xac", 1, &state));
    TEST_ASSERT_EQUAL_HEX32(0x20ac, character);
}

void test_mbrtowc_rejects_invalid_utf8(void)
{
    wchar_t character = 0;
    mbstate_t state = {0};

    errno = 0;
    TEST_ASSERT_EQUAL_UINT((size_t)-1, mbrtowc(&character, "\xc0\x80", 2, &state));
    TEST_ASSERT_EQUAL_INT(EILSEQ, errno);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, mbrtowc(&character, "\xed\xa0\x80", 3, &state));
    TEST_ASSERT_EQUAL_INT(EILSEQ, errno);
}

void test_wcrtomb_encodes_utf8_codepoints(void)
{
    char bytes[5] = {0};
    mbstate_t state = {0};

    TEST_ASSERT_EQUAL_UINT(4, wcrtomb(bytes, 0x1f600, &state));
    TEST_ASSERT_EQUAL_MEMORY("\xf0\x9f\x98\x80", bytes, 4);
}

void test_string_conversions_round_trip_utf8(void)
{
    const char input[] = "A\xe2\x82\xac\xf0\x9f\x98\x80";
    const char * multibyte = input;
    wchar_t wide[4] = {0};
    mbstate_t state = {0};

    TEST_ASSERT_EQUAL_UINT(3, mbsrtowcs(wide, &multibyte, 4, &state));
    TEST_ASSERT_NULL(multibyte);
    TEST_ASSERT_EQUAL_HEX32(0x20ac, wide[1]);
    TEST_ASSERT_EQUAL_HEX32(0x1f600, wide[2]);

    char output[sizeof(input)] = {0};
    const wchar_t * wide_source = wide;
    state = (mbstate_t){0};
    TEST_ASSERT_EQUAL_UINT(sizeof(input) - 1, wcsrtombs(output, &wide_source, sizeof(output), &state));
    TEST_ASSERT_NULL(wide_source);
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

void test_nonrestartable_string_conversions_measure_and_round_trip(void)
{
    const char input[] = "A\xe2\x82\xac";
    wchar_t wide[3] = {0};
    char output[sizeof(input)] = {0};

    TEST_ASSERT_EQUAL_UINT(2, mbstowcs(NULL, input, 0));
    TEST_ASSERT_EQUAL_UINT(2, mbstowcs(wide, input, 3));
    TEST_ASSERT_EQUAL_UINT(sizeof(input) - 1, wcstombs(NULL, wide, 0));
    TEST_ASSERT_EQUAL_UINT(sizeof(input) - 1, wcstombs(output, wide, sizeof(output)));
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

void test_string_conversions_report_an_unwritten_terminator(void)
{
    const char input[] = "A";
    const char * multibyte = input;
    wchar_t wide[1] = {0};
    mbstate_t state = {0};

    TEST_ASSERT_EQUAL_UINT(1, mbsrtowcs(wide, &multibyte, 1, &state));
    TEST_ASSERT_EQUAL_PTR(input + 1, multibyte);

    const wchar_t wide_input[] = L"A";
    const wchar_t * wide_source = wide_input;
    char output[1] = {0};
    TEST_ASSERT_EQUAL_UINT(1, wcsrtombs(output, &wide_source, 1, &state));
    TEST_ASSERT_EQUAL_PTR(wide_input + 1, wide_source);
}

void test_wcwidth_handles_combining_and_wide_characters(void)
{
    TEST_ASSERT_EQUAL_INT(1, wcwidth(L'A'));
    TEST_ASSERT_EQUAL_INT(0, wcwidth(0x0301));
    TEST_ASSERT_EQUAL_INT(2, wcwidth(0x4e2d));
    TEST_ASSERT_EQUAL_INT(2, wcwidth(0x1f600));
    TEST_ASSERT_EQUAL_INT(-1, wcwidth(L'\n'));
}

void test_wide_classification_and_case_conversion(void)
{
    TEST_ASSERT_TRUE(iswalpha(0x03a9));
    TEST_ASSERT_TRUE(iswdigit(0x0664));
    TEST_ASSERT_TRUE(iswspace(0x3000));
    TEST_ASSERT_EQUAL_HEX32(0x03c9, towlower(0x03a9));
    TEST_ASSERT_EQUAL_HEX32(0x0416, towupper(0x0436));
    TEST_ASSERT_TRUE(iswctype(L'A', wctype("upper")));
}

void test_wide_string_primitives_find_copy_and_compare(void)
{
    wchar_t copy[8] = {0};
    const wchar_t text[] = L"alpha";

    TEST_ASSERT_EQUAL_PTR(text + 3, wcschr(text, L'h'));
    TEST_ASSERT_EQUAL_PTR(text, wmemchr(text, L'a', 5));
    TEST_ASSERT_NULL(wmemchr(text, L'z', 5));
    TEST_ASSERT_EQUAL_PTR(text + 5, wcschr(text, L'\0'));
    TEST_ASSERT_NULL(wcschr(text, L'z'));
    TEST_ASSERT_EQUAL_PTR(copy, wcscpy(copy, text));
    TEST_ASSERT_EQUAL_INT(0, wcscmp(copy, text));
    TEST_ASSERT_EQUAL_INT(0, wcsncmp(L"alphabet", L"alpha", 5));
    TEST_ASSERT_TRUE(wcsncmp(L"alpha", L"alphi", 5) < 0);
    TEST_ASSERT_TRUE(wcscoll(L"alpha", L"beta") < 0);
}

void test_mbsinit_tracks_partial_conversion_state(void)
{
    wchar_t character = 0;
    mbstate_t state = {0};

    TEST_ASSERT_TRUE(mbsinit(&state));
    TEST_ASSERT_EQUAL_UINT((size_t)-2, mbrtowc(&character, "\xe2", 1, &state));
    TEST_ASSERT_FALSE(mbsinit(&state));
    TEST_ASSERT_EQUAL_UINT(2, mbrtowc(&character, "\x82\xac", 2, &state));
    TEST_ASSERT_TRUE(mbsinit(&state));
}
