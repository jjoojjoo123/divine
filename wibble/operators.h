// -*- C++ -*-

#include <set>
#include <wibble/empty.h>
#include <wibble/singleton.h>
#include <algorithm>

#ifndef WIBBLE_OPERATORS_H
#define WIBBLE_OPERATORS_H

namespace wibble {
namespace operators {

/*
template< typename S, typename VT > struct IsContainer {
    typedef S T;
};

template< typename S >
typename IsContainer< S, typename S::value_type >::T operator &&( const S &a, const S &b ) {
    S ret;
    std::set_intersection( a.begin(), a.end(), b.begin(), b.end(),
                           std::inserter( ret, ret.begin() ) );
    return ret;
}
*/

template< typename T >
T operator+( const T &i, typename T::difference_type o ) {
    T r = i;
    std::advance( r, o );
    return r;
}

template< typename T >
std::set< T > operator &( const std::set< T > &a, const std::set< T > &b ) {
    std::set< T > ret;
    std::set_intersection( a.begin(), a.end(), b.begin(), b.end(),
                           std::inserter( ret, ret.begin() ) );
    return ret;
}

template< typename T >
std::set< T > operator &( const std::set< T > &a, const T &b ) {
    std::set< T > ret;
    if ( a.find( b ) != a.end() ) {
        std::set< T > r;
        r.insert( b );
        return r;
    }
    return std::set< T >();
}

template< typename T >
std::set< T > operator |( const std::set< T > &a, const T& item ) {
    std::set< T > ret = a;
    ret.insert(item);
    return ret;
}

template< typename T >
std::set< T > operator |( const std::set< T > &a, const wibble::Empty<T>& ) {
    return a;
}

template< typename T >
std::set< T > operator |( const std::set< T > &a, const wibble::Singleton<T>& item ) {
    std::set< T > ret = a;
    ret.insert(*item.begin());
    return ret;
}

template< typename T >
std::set< T > operator |( const std::set< T > &a, const std::set< T > &b ) {
    std::set< T > ret;
    std::set_union( a.begin(), a.end(), b.begin(), b.end(),
                    std::inserter( ret, ret.begin() ) );
    return ret;
}

template< typename T >
std::set< T > operator -( const std::set< T > &a, const std::set< T > &b ) {
    std::set< T > ret;
    std::set_difference( a.begin(), a.end(), b.begin(), b.end(),
                         std::inserter(ret, ret.begin() ) );
    return ret;
}

template< typename T >
std::set< T > operator -( const std::set< T > &a, const T& item ) {
    std::set< T > ret = a;
    ret.erase(item);
    return ret;
}

template< typename T >
std::set< T > operator -( const std::set< T > &a, const wibble::Singleton<T>& item ) {
    std::set< T > ret = a;
    ret.erase(*item.begin());
    return ret;
}

template< typename T >
std::set< T > operator -( const std::set< T > &a, const wibble::Empty<T>& ) {
    return a;
}

template< typename T >
std::set< T > &operator|=( std::set< T > &a, const wibble::Empty<T>& )
{
    return a;
}

template< typename T >
std::set< T > &operator|=( std::set< T > &a, const T& item )
{
    a.insert(item);
    return a;
}

template< typename T >
std::set< T > &operator|=( std::set< T > &a, const wibble::Singleton<T>& item )
{
    a.insert(*item.begin());
    return a;
}

template< typename T >
std::set< T > &operator |=( std::set< T > &a, const std::set< T > &b ) {
    std::set< T > ret;
    ret = a | b;
    return a = ret;
}

template< typename T >
std::set< T > &operator &=( std::set< T > &a, const std::set< T > &b ) {
    std::set< T > ret;
    ret = a & b;
    return a = ret;
}

template< typename T >
std::set< T > &operator-=( std::set< T > &a, const wibble::Empty<T>& )
{
    return a;
}

template< typename T >
std::set< T > &operator-=( std::set< T > &a, const T& item )
{
    a.erase(item);
    return a;
}

template< typename T >
std::set< T > &operator-=( std::set< T > &a, const wibble::Singleton<T>& item )
{
    a.erase(*item.begin());
    return a;
}

template< typename T >
std::set< T > &operator -=( std::set< T > &a, const std::set< T > &b )
{
    std::set< T > ret;
    ret = a - b;
    return a = ret;
}

template< typename T >
bool operator<=( const T &a, const std::set< T > &b ) {
    return b.find( a ) != b.end();
}

template< typename T >
bool operator<=( const std::set< T > &a, const std::set< T > &b ) {
	typename std::set<T>::const_iterator x = a.begin();

	for ( typename std::set<T>::const_iterator y = b.begin(); y != b.end(); ++y )
		if ( x == a.end() )
			return true;
		else if (*x == *y)
			++x;
		else if (*x < *y)
			return false;

	return x == a.end();
}

}
}

#endif
