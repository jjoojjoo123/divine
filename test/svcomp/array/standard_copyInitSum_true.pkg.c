/* TAGS: c sym */
/* VERIFY_OPTS: --symbolic --sequential -o nofail:malloc */
/* CC_OPTS: */

// V: v.10 CC_OPT: -DSIZE=10
// V: v.100 CC_OPT: -DSIZE=100
// V: v.1000 CC_OPT: -DSIZE=1000
// V: v.10000 CC_OPT: -DSIZE=10000 TAGS: big
// V: v.100000 CC_OPT: -DSIZE=100000 TAGS: big
extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_error() __attribute__ ((__noreturn__));
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: __VERIFIER_error(); } }

int main ( ) {
  int a [SIZE];
  int b [SIZE];
  int incr = __VERIFIER_nondet_int();
  int i = 0;
  while ( i < SIZE ) {
    a[i] = 42;
    i = i + 1;
  }

  for ( i = 0 ; i < SIZE ; i++ ) {
    b[i] = a[i];
  }

  for ( i = 0 ; i < SIZE ; i++ ) {
    b[i] = b[i] + incr;
  }

  int x;
  for ( x = 0 ; x < SIZE ; x++ ) {
    __VERIFIER_assert(  b[x] == 42 + incr  );
  }
  return 0;
}
