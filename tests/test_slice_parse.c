#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/shell/slice_parse.h"

int main(void) {
    uint32_t value = 42u;
    assert(slice_parse_ms("1", &value) == 0 && value == 1u);
    assert(slice_parse_ms("200", &value) == 0 && value == 200u);
    assert(slice_parse_ms("1500", &value) == 0 && value == 1500u);
    assert(slice_parse_ms("10000", &value) == 0 && value == 10000u);

    const char *invalid[] = {
        "", "0", "10001", "-1", "+200", "20ms", "abc",
        "4294977296", "999999999999999999999999999999999999"
    };
    for (uint32_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        value = 42u;
        assert(slice_parse_ms(invalid[i], &value) == -1);
        assert(value == 42u);
    }
    assert(slice_parse_ms(0, &value) == -1);
    assert(slice_parse_ms("200", 0) == -1);
    puts("slice_parse: PASS");
    return 0;
}
