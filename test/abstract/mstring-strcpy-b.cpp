/* TAGS: mstring min sym */
/* VERIFY_OPTS: --symbolic -o nofail:malloc */

#include <rst/domains.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

void my_strcpy( char * dest, const char * src ) {
    int i = 0;
    while ( src[i] != '\0' ) {
        dest[i] = src[i];
        ++i;
    }

    dest[i] = '\0';
}

int main() {
    char buff1[7] = "string";
    const char * src = __mstring_from_string( buff1);
    char buff2[7] = "";
    char * dst = __mstring_from_string( buff2 );

    my_strcpy( dst, src );

    assert( strcmp( src,dst ) == 0 );
}
