#include <stdio.h>

#include "tests.h"

int main() {
    int failed = 0;
    failed += test1();
    failed += test2();
    failed += test3();
    failed += test4();
    failed += test5();

    if (failed == 0) {
        printf("All tests passed!\n");
    } else {
        printf("%d test(s) failed\n", failed);
    }

    return failed;
}
