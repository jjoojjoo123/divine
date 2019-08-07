/* TAGS: mstring min sym todo */
/* VERIFY_OPTS: --symbolic -o nofail:malloc */

#include <rst/domains.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    char buffe[7] = "aaabbb";
    char * expected = __mstring_val( buffe, 7 );

    char buffa[7] = "aaa";
    char * a = __mstring_val( buffa, 7 );
    char buffb[7] = "bbb";
    char * b = __mstring_val( buffb, 4 );

    strcat(a, b);

    assert( strcmp( a, expected ) == 0 );
}
