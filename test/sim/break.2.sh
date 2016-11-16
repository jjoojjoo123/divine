. lib/testcase

SRC=$TESTS/c/assert.1.c

sim $SRC <<EOF
+ ^# executing __boot
> start
+ ^# executing main
> break _PDCLIB_assert_dios
> break --list
+ ^ 1: _PDCLIB_assert
> stepa
+ ^# executing _PDCLIB_assert_dios
EOF

sim $SRC <<EOF
+ ^# executing __boot
> break $SRC:5
> break --list
+ ^ 1: $SRC:5
> stepa --count 10
+ ^# executing main at $SRC:5
EOF

sim $SRC <<EOF
+ ^# executing __boot
> break assert.1.c:5
> break --list
+ ^ 1: assert.1.c:5
> stepa --count 10
+ ^# executing main at $SRC:5
EOF
