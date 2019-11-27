/* TAGS: todo sym c++ */
/* VERIFY_OPTS: --symbolic */
/* CC_OPTS: -O1 */

#include <cassert>

extern "C" {
int __sym_val_i32();
}

struct S { int a, b, c; };

S foo() {
    S s;
    s.b = __sym_val_i32();
    return s;
}

int main() {
    assert( foo().b == 0 ); /* ERROR */
}
