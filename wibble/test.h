// -*- C++ -*-

#include <wibble/string.h>
#include <iostream>
#include <cstdlib>

#ifndef WIBBLE_TEST_H
#define WIBBLE_TEST_H

// TODO use TLS
extern int assertFailure;

struct Location {
    const char *file;
    int line;
    std::string stmt;
    Location( const char *f, int l, std::string st )
        : file( f ), line( l ), stmt( st ) {}
};

#define LOCATION(stmt) Location( __FILE__, __LINE__, stmt )
#define assert(x) assert_fn( LOCATION( #x ), x )
#define assert_eq(x, y) assert_eq_fn( LOCATION( #x " == " #y ), x, y )
#define assert_neq(x, y) assert_neq_fn( LOCATION( #x " != " #y ), x, y )
#define assert_list_eq(x, y) \
    assert_list_eq_fn( LOCATION( #x " == " #y ), \
                       sizeof( y ) / sizeof( y[0] ), x, y )

struct AssertFailed {
    std::ostream &stream;
    std::ostringstream str;
    bool expect;
    AssertFailed( Location l, std::ostream &s = std::cerr )
        : stream( s )
    {
        expect = assertFailure > 0;
        str << l.file << ": " << l.line
            << ": assertion `" << l.stmt << "' failed;";
    }

    ~AssertFailed() {
        if ( expect )
            ++assertFailure;
        else {
            stream << str.str() << std::endl;
            abort();
        }
    }
};

template< typename X >
inline AssertFailed &operator<<( AssertFailed &f, X x )
{
    f.str << x;
    return f;
}

template< typename X >
void assert_fn( Location l, X x )
{
    if ( !x ) {
        AssertFailed f( l );
    }
}

template< typename X, typename Y >
void assert_eq_fn( Location l, X x, Y y )
{
    if ( !( x == y ) ) {
        AssertFailed f( l );
        f << " got ["
          << x << "] != [" << y
          << "] instead";
    }
}

template< typename X >
void assert_list_eq_fn(
    Location loc, int c, X l, const typename X::Type check[] )
{
    int i = 0;
    while ( !l.empty() ) {
        if ( l.head() != check[ i ] ) {
            AssertFailed f( loc );
            f << " list disagrees at position "
              << i << ": [" << wibble::str::fmt( l.head() )
              << "] != [" << wibble::str::fmt( check[ i ] )
              << "]";
        }
        l = l.tail();
        ++ i;
    }
    if ( i != c ) {
        AssertFailed f( loc );
        f << " got ["
          << i << "] != [" << c << "] instead";
    }
}

template< typename X, typename Y >
void assert_neq_fn( Location l, X x, Y y )
{
    if ( x != y )
        return;
    AssertFailed f( l );
    f << " got ["
      << x << "] == [" << y << "] instead";
}

inline void beginAssertFailure() {
    assertFailure = 1;
}

inline void endAssertFailure() {
    int f = assertFailure;
    assertFailure = 0;
    assert( f > 1 );
}

struct ExpectFailure {
    ExpectFailure() { beginAssertFailure(); }
    ~ExpectFailure() { endAssertFailure(); }
};

typedef void Test;

#endif
