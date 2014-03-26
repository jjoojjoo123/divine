// -*- C++ -*- Substitution Failure Is Not An Error

#include <wibble/config.h>

#ifndef WIBBLE_SFINAE_H
#define WIBBLE_SFINAE_H

namespace wibble {

struct Unit {
    bool operator<( Unit ) const { return false; }
    bool operator==( Unit ) const { return true; }
};

struct TTrue {
    static const bool value = true;
};

struct TFalse {
    static const bool value = false;
};

// small SFINAE utilities, we probably prefer to avoid full weight of boost here
template< typename A, typename B >
struct TSame {
    static const bool value = false;
};

template< typename A >
struct TSame< A, A > {
    static const bool value = true;
};

template< bool, bool, bool = true, bool = true, bool = true >
struct TAndC {
    static const bool value = false;
};

template<>
struct TAndC< true, true, true, true, true > {
    static const bool value = true;
};

template< typename A, typename B,
          typename C = TTrue, typename D = TTrue, typename E = TTrue >
struct TAnd : TAndC< A::value, B::value, C::value, D::value, E::value > {};

template< bool, bool, bool = false, bool = false, bool = false >
struct TOrC {
    static const bool value = true;
};

template<>
struct TOrC< false, false, false, false, false > {
    static const bool value = false;
};

template< typename A, typename B,
          typename C = TFalse, typename D = TFalse, typename E = TFalse >
struct TOr : TOrC< A::value, B::value, C::value, D::value, E::value > {};

/* template< typename T >
struct IsT {
    static const bool value = true;
    }; */

template< bool a > struct TNotC {
    static const bool value = !a;
};

template< typename T > struct TNot : TNotC< T::value > {};

template< bool a, bool b >
struct TImplyC : TNot< TAndC< a, TNotC< b >::value > > {};

template< typename A, typename B >
struct TImply : TImplyC< A::value, B::value > {};

template< bool, typename T = Unit >
struct EnableIfC {};

template< typename Type >
struct EnableIfC< true, Type > { typedef Type T; };

template< bool, typename T = Unit >
struct DisableIfC {};

template< typename Type >
struct DisableIfC< false, Type > { typedef Type T; };

template< typename X, typename T = Unit >
struct EnableIf : EnableIfC< X::value, T > {};

template< typename X, typename T = Unit >
struct DisableIf : DisableIfC< X::value, T > {};

template< typename A, typename B >
struct TPair {
    typedef A First;
    typedef B Second;
};

struct Preferred { WIBBLE_CONSTEXPR Preferred() { } };
struct NotPreferred { WIBBLE_CONSTEXPR NotPreferred( Preferred ) {} };

#if WIBBLE_CPP_11

template< typename... >
struct CompilationStopper {
    static constexpr bool stop = false;
};

template< typename R, typename... X >
R declcheck( X... ) {
    static_assert( CompilationStopper< R >::stop, "declcheck() must not be used!" );
}

#endif

}

#endif
