#include "unit.h"
int unit_failures, unit_checks;
int main(void)
{
    unit_main();
    printf("%d checks, %d failures\n", unit_checks, unit_failures);
    return unit_failures ? 1 : 0;
}
