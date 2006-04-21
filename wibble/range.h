/** -*- C++ -*-
    @file wibble/range.h
    @author Peter Rockai <me@mornfall.net>
*/

#include <iostream> // for noise
#include <iterator>
#include <vector>
#include <set>
#include <algorithm>
#include <ext/algorithm>

#include <wibble/iterator.h>
#include <wibble/shared.h>
#include <wibble/exception.h>

#ifndef WIBBLE_RANGE_H
#define WIBBLE_RANGE_H

namespace wibble {

template< typename > struct Range;
template< typename > struct Consumer;

template< typename R >
struct RangeIterator : R {
    RangeIterator() {}
    RangeIterator( const R &r ) : R( r ) {}
public:
    RangeIterator &operator++() { R::operator++(); return *this; }
    RangeIterator operator++(int) { return R::operator++(0); }
    bool operator==( const RangeIterator &r ) {
        return R::operator==( r );
    }
};

template< typename T >
struct RangeInterface : IteratorInterface< T > {
    virtual void setToEnd() = 0;
    virtual ~RangeInterface() {}
};

template< typename T, typename Self, typename Interface = RangeInterface< T > >
struct RangeImpl: IteratorImpl< T, Self, Interface >
{
    typedef IteratorImpl< T, Self, Interface > Base;
    typedef RangeIterator< Self > iterator;
    friend struct RangeIterator< Self >;

    iterator begin() const { return iterator( this->self() ); } // STL-style iteration
    iterator end() const { Self e( this->self() ); e.setToEnd(); return iterator( e ); }
    Range< T > sorted() const;

    void output( Consumer< T > t ) const {
        std::copy( begin(), end(), t );
    }

    virtual bool empty() const {
        return begin() == end();
    }

    virtual ~RangeImpl() {}
protected:
    Self &operator++() { return Base::operator++(); }
    Self operator++(int) { return Base::operator++(0); }
};

template< typename T >
struct Range : Amorph< Range< T >, RangeInterface< T >, 0 >,
               RangeImpl< T, Range< T > >
{
    typedef Amorph< Range< T >, RangeInterface< T >, 0 > Super;
    typedef T ElementType;

    Range( const RangeInterface< T > &i ) : Super( i ) {}
    Range() {}

    T current() const { return this->implInterface()->current(); }
    virtual void advance() { this->implInterface()->advance(); }
    virtual void setToEnd() { this->implInterface()->setToEnd(); }
    virtual bool empty() const { return !this->implInterface() || this->begin() == this->end(); }

    template< typename C > operator Range< C >();
};

template< typename R >
Range< typename R::ElementType > range( R r ) {
    return r;
}

}

// ----- individual range implementations follow
#include <wibble/consumer.h>

namespace wibble {

// sfinae: substitution failure is not an error
template< typename It >
struct IteratorRange : public RangeImpl<
    typename std::iterator_traits< It >::value_type,
    IteratorRange< It > >
{
    typedef typename std::iterator_traits< It >::value_type Value;

    IteratorRange( It c, It e )
        : m_current( c ), m_end( e ) {}

    virtual Value current() const { return *m_current; }
    virtual void advance() { ++m_current; }

    bool operator==( const IteratorRange &r ) const {
        return r.m_current == m_current && r.m_end == m_end;
    }

    void setToEnd() {
        m_current = m_end;
    }

protected:
    It m_current, m_end;
};

template< typename T, typename Casted >
struct CastedRange : public RangeImpl< T, CastedRange< T, Casted > >
{
    CastedRange( Range< Casted > r ) : m_casted( r ) {}
    virtual T current() const {
        return static_cast< T >( m_casted.current() );
    }
    virtual void advance() { m_casted.advance(); }

    bool operator==( const CastedRange &r ) const {
        return m_casted == r.m_casted;
    }

    void setToEnd() {
        m_casted = m_casted.end();
    }

protected:
    Range< Casted > m_casted;
};

template< typename T, typename C >
Range< T > castedRange( C r ) {
    return CastedRange< T, typename C::ElementType >( r );
}

// compat
template< typename T, typename C >
Range< T > upcastRange( C r ) {
    return CastedRange< T, typename C::ElementType >( r );
}

template< typename T> template< typename C >
Range< T >::operator Range< C >() {
    return castedRange< C >( *this );
}

template< typename In >
Range< typename In::value_type > range( In b, In e ) {
    return IteratorRange< In >( b, e );
}

template< typename T >
struct IntersectionRange : RangeImpl< T, IntersectionRange< T > >
{
    IntersectionRange() {}
    IntersectionRange( Range< T > r1, Range< T > r2 )
        : m_first( r1 ), m_second( r2 ),
        m_valid( false )
    {
    }

    void find() const {
        if (!m_valid) {
            while ( m_first != m_first.end()
                    && m_second != m_second.end() ) {
                if ( *m_first < *m_second )
                    m_first.advance();
                else if ( *m_second < *m_first )
                    m_second.advance();
                else break; // equal
            }
            if ( m_second.empty() ) m_first.setToEnd();
            else if ( m_first.empty() ) m_second.setToEnd();
        }
        m_valid = true;
    }

    virtual void advance() {
        find();
        m_first.advance();
        m_second.advance();
        m_valid = false;
    }

    virtual T current() const {
        find();
        return m_first.current();
    }

    void setToEnd() {
        m_first = m_first.end();
        m_second = m_second.end();
    }

    bool operator==( const IntersectionRange &f ) const {
        find();
        f.find();
        return m_first == f.m_first;
        // return m_pred == f.m_pred && m_range == f.m_range;
    }

protected:
    mutable Range< T > m_first, m_second;
    mutable bool m_valid:1;
};

template< typename R >
IntersectionRange< typename R::ElementType > intersectionRange( R r1, R r2 ) {
    return IntersectionRange< typename R::ElementType >( r1, r2 );
}

template< typename R, typename Pred >
struct FilteredRange : RangeImpl< typename R::ElementType,
                                  FilteredRange< R, Pred > >
{
    typedef typename R::ElementType ElementType;
    FilteredRange( const R &r, Pred p ) : m_range( r ), m_pred( p ),
        m_valid( false ) {}

    void find() const {
        if (!m_valid)
            m_range = std::find_if( m_range.begin(), m_range.end(), m_pred );
        m_valid = true;
    }

    virtual void advance() {
        find();
        m_range.advance();
        m_valid = false;
    }

    virtual ElementType current() const {
        find();
        return m_range.current();
    }

    void setToEnd() {
        m_range = m_range.end();
    }

    bool operator==( const FilteredRange &f ) const {
        find();
        f.find();
        return m_range == f.m_range;
        // return m_pred == f.m_pred && m_range == f.m_range;
    }

protected:
    mutable R m_range;
    Pred m_pred;
    mutable bool m_valid:1;
};

template< typename R, typename Pred >
FilteredRange< R, Pred > filteredRange(
    R r, Pred p ) {
    return FilteredRange< R, Pred >( r, p );
}

template< typename T >
struct UniqueRange : RangeImpl< T, UniqueRange< T > >
{
    UniqueRange() {}
    UniqueRange( Range< T > r ) : m_range( r ), m_valid( false ) {}

    void find() const {
        if (!m_valid)
            while ( m_range != m_range.end()
                    && m_range.next() != m_range.end()
                    && *m_range == *m_range.next() )
                m_range = m_range.next();
        m_valid = true;
    }

    virtual void advance() {
        find();
        m_range.advance();
        m_valid = false;
    }

    virtual T current() const {
        find();
        return m_range.current();
    }

    void setToEnd() {
        m_range = m_range.end();
    }

    bool operator==( const UniqueRange &r ) const {
        find();
        r.find();
        return m_range == r.m_range;
    }

protected:
    mutable Range< T > m_range;
    mutable bool m_valid:1;
};

template< typename R >
UniqueRange< typename R::ElementType > uniqueRange( R r1 ) {
    return UniqueRange< typename R::ElementType >( r1 );
}

template< typename Transform >
struct TransformedRange : RangeImpl< typename Transform::result_type,
                                     TransformedRange< Transform > >
{
    typedef typename Transform::argument_type Source;
    typedef typename Transform::result_type Result;
    TransformedRange( Range< Source > r, Transform t )
        : m_range( r ), m_transform( t ) {}

    bool operator==( const TransformedRange &o ) const {
        return m_range == o.m_range; // XXX buaaaahaaa!
    }

    Result current() const {
        return m_transform( m_range.current() );
    }

    void advance() {
        m_range.advance();
    }

    void setToEnd() {
        m_range = m_range.end();
    }

protected:
    Range< Source > m_range;
    Transform m_transform;
};

template< typename Trans >
TransformedRange< Trans > transformedRange(
    Range< typename Trans::argument_type > r, Trans t ) {
    return TransformedRange< Trans >( r, t );
}

template< typename Container >
struct BackedRange :
    RangeImpl< typename Container::value_type, BackedRange< Container > >
// ConsumerImpl< typename Cont::value_type, BackedRange< Cont > >
{
    typedef typename Container::value_type T;
    typedef BackedRange< Container > This;
    struct SharedContainer : Container, SharedBase {
        template< typename I >
        SharedContainer( I a, I b ) : Container( a, b ) {}
    };
    virtual void advance() { ++m_position; }

    virtual T current() const {
        return *m_position;
    }
    virtual void setToEnd() {
        m_position = m_container->end();
    }

protected:
    typedef SharedPtr< SharedContainer > ContainerPtr;
    typedef typename Container::const_iterator Position;
    Position m_position;
    ContainerPtr m_container;
};

template< typename T, typename Self >
struct RandomAccessImpl {
};

template< typename T, typename Self >
struct MutableImpl {
};

// XXX using VectorRange as an output iterator compiles but DOES NOT
// WORK (segfaults)... you can use consumer( myPetVectorRange )
// this needs fixing though (iow, it specifically should not compile)
// same issue as with stl, really
template< typename T >
struct VectorRange : RangeImpl< T, VectorRange< T > >,
                     virtual ConsumerInterface< T >
{
    typedef std::random_access_iterator_tag iterator_category;
    VectorRange() : m_vector( new SharedVector ), m_position( 0 ) {}
    VectorRange( const Range< T > &i ) {
        RangeImpl< T, VectorRange< T > >::initFromBase( i.impl() );
    }

    virtual void consume( const T &a ) {
        m_vector->push_back( a );
    }

    virtual void advance() {
        ++m_position;
    }

    virtual T current() const {
        return m_vector->operator[]( m_position );
    }

    void setToEnd() {
        m_position = std::distance( m_vector->begin(), m_vector->end() );
    }

    bool operator==( const VectorRange &r ) const {
        return m_position == r.m_position;
    }

    VectorRange &operator+=( ptrdiff_t off ) {
        m_position += off;
        return *this;
    }

    VectorRange &operator--() {
        --m_position;
        return *this;
    }

    VectorRange operator--( int ) {
        VectorRange tmp( *this );
        --m_position;
        return tmp;
    }

    ptrdiff_t operator-( const VectorRange &r ) {
        return m_position - r.m_position;
    }

    VectorRange operator-( ptrdiff_t off ) {
        VectorRange< T > r( *this );
        r.m_position = m_position - off;
        return r;
    }

    VectorRange operator+( ptrdiff_t off ) {
        VectorRange< T > r( *this );
        r.m_position = m_position + off;
        return r;
    }

    bool operator<( const VectorRange &r ) {
        return m_position < r.m_position;
    }

    size_t size() const {
        return m_vector->size() - m_position;
    }

    T &operator*() {
        return m_vector->operator[]( m_position );
    }

    const T &operator*() const {
        return m_vector->operator[]( m_position );
    }

    void clear() {
        m_vector->clear();
        m_position = 0;
    }

protected:
    typedef Shared< std::vector< T > > SharedVector;
    typedef SharedPtr< SharedVector > VectorPointer;
    VectorPointer m_vector;
    ptrdiff_t m_position;
};

template< typename T, typename S, typename I >
Range< T > RangeImpl< T, S, I >::sorted() const
{
    VectorRange< T > out;
    output( consumer( out ) );
    std::sort( out.begin(), out.end() );
    return out;
}

template< typename T, typename _Advance, typename _End >
struct GeneratedRange : RangeImpl< T, GeneratedRange< T, _Advance, _End > >
{
    typedef _Advance Advance;
    typedef _End End;

    GeneratedRange() {}
    GeneratedRange( const T &t, const Advance &a, const End &e )
        : m_current( t ), m_advance( a ), m_endPred( e ), m_end( false )
    {
    }

    void advance() {
        m_advance( m_current );
    }

    void setToEnd() {
        m_end = true;
    }

    T current() const { return m_current; }

    bool isEnd() const { return m_end || m_endPred( m_current ); }

    bool operator==( const GeneratedRange &r ) const {
        if ( isEnd() && r.isEnd() ) return true;
        if ( isEnd() || r.isEnd() ) return false;
        return m_current == r.m_current;
    }

protected:
    T m_current;
    Advance m_advance;
    End m_endPred;
    bool m_end;
};

template< typename T, typename A, typename E >
GeneratedRange< T, A, E > generatedRange( T t, A a, E e )
{
    return GeneratedRange< T, A, E >( t, a, e );
}

}

#endif
