// -*- C++ -*-
//             (c) 2013 Vladimír Štill <xstill@fi.muni.cz>
#include <utility>
#include <memory>
#include <iterator>
#include <tuple>

#include <divine/utility/statistics.h>
#include <divine/toolkit/sharedhashset.h>
#include <divine/toolkit/treecompressedhashset.h>
#include <divine/toolkit/ntreehashset.h>
#include <divine/toolkit/hashset.h>
#include <divine/toolkit/pool.h>
#include <divine/toolkit/blob.h>
#include <divine/toolkit/parallel.h> // for WithID

#ifndef DIVINE_STORE_H
#define DIVINE_STORE_H

namespace divine {
namespace visitor {

/* Global utility functions independent of store type
 */

template< typename T >
struct IsNew : T {
    T &base() { return *this; }
    bool _isnew;
    IsNew( const T &t, bool isnew ) : T( t ), _isnew( isnew ) {}
    bool isnew() { return _isnew; }
    operator T() { return base(); }

    template< typename F >
    auto map( F f ) -> IsNew< typename std::result_of< F( T ) >::type >
    {
        return isNew( f( base() ), _isnew );
    }
};

template< typename T >
IsNew< T > isNew( const T &x, bool y ) {
    return IsNew< T >( x, y );
}

template < typename TableProvider, typename Statistics >
struct StoreCommon : TableProvider
{
    using Table = typename TableProvider::Table;
    using Hasher = typename Table::Hasher;
    using InsertItem = typename Table::InsertItem;
    using StoredItem = typename Table::StoredItem;

  protected: // we don't want algoritms to mess with table directly

  public:
    template< typename... Args >
    StoreCommon( Pool &p, Args&&... args )
        : TableProvider( std::forward< Args >( args )... ), _pool( p ) {}

    Pool &_pool;

    Hasher& hasher() { return this->table().hasher; }
    Pool& pool() { return _pool; }

    hash_t hash( InsertItem node ) { return hasher().hash( node ); }
    int slack() { return hasher().slack; }
    bool valid( InsertItem n ) { return hasher().valid( n ); }
    bool equal( InsertItem m, InsertItem n ) { return hasher().equal( m, n ); }
    bool has( InsertItem n ) { return this->table().has( n ); }
    void setSize( int size ) { this->table().setSize( size ); }

    int owner( hash_t h ) { return TableProvider::owner( h ); }
    int owner( InsertItem n, hash_t hint = 0 ) {
        return owner( hint ? hint : hash( n ) );
    }

    bool knows( hash_t h ) { return TableProvider::knows( h ); }
    bool knows( InsertItem n, hash_t hint = 0 ) {
        return TableProvider::knows( hint ? hint : hash( n ) );
    }

    IsNew< StoredItem > _fetch( InsertItem s, hash_t h ) {
        StoredItem found;
        bool had;
        std::tie( found, had ) = this->table().getHinted( s, h ? h : hash( s ) );
        assert( hasher().valid( found ) == had );
        return isNew( found, had );
    }

    template< typename X >
    IsNew< StoredItem > _store( InsertItem s, hash_t h, X &x ) {
        StoredItem s2;
        bool inserted;
        std::tie( s2, inserted ) = this->table().insertHinted( s, h ? h : hash( s ), x );
        if ( inserted ) {
            Statistics::global().hashadded( this->id(), memSize( s, hasher().pool() ) );
            Statistics::global().hashsize( this->id(), this->table().size() );
        }
        return isNew( s2, inserted );
    }
};

template< typename Generator, typename Hasher >
struct IdentityWrap {
    template< template< typename, typename > class T >
    using Make = T< typename Generator::Node, Hasher >;
};

template< typename Generator, typename Hasher >
struct NTreeWrap {
    template< template < typename, typename > class T >
    using Make = NTreeHashSet< T, typename Generator::Node, Hasher >;
};

struct ProviderCommon {
    WithID* _id;
    void setId( WithID& id ) { _id = &id; }

    int id() { assert( _id ); return _id->id(); }
    int owner( hash_t hash ) const { return hash % _id->peers(); }
    bool knows( hash_t hash ) const { return owner( hash ) == _id->id(); }
    ProviderCommon() : _id( nullptr ) {}
};

template< typename Provider, typename Generator, typename Hasher >
using NTreeTable = typename Provider::template Make< NTreeWrap< Generator, Hasher > >;

template< typename Provider, typename Generator, typename Hasher >
using PlainTable = typename Provider::template Make< IdentityWrap< Generator, Hasher > >;

struct PartitionedProvider {
    template < typename WrapTable >
    struct Make : ProviderCommon {
        using Table = typename WrapTable::template Make< HashSet >;
        Table _table;

        Table &table() { return _table; }
        const Table &table() const { return _table; }

        Make( typename Table::InputHasher h, Make * ) : _table( h ) {}
    };
};

struct SharedProvider {
    template < typename WrapTable >
    struct Make : ProviderCommon {
        using Table = typename WrapTable::template Make< SharedHashSet >;
        using TablePtr = std::shared_ptr< Table >;

        TablePtr _table;

        Table &table() { return *_table; }
        const Table &table() const { return *_table; }

        bool knows( hash_t ) const { return true; } // we know all
        int owner( hash_t ) const {
            assert_unreachable( "no owners in shared store" );
        }

        Make ( typename Table::InputHasher h, Make *master )
            : _table( master ? master->_table : std::make_shared< Table >( h ) )
        {}
    };
};

#define STORE_ITERATOR using Iterator = StoreIterator< This >; \
    Iterator begin() { return Iterator( *this, 0 ); } \
    Iterator end() { return Iterator( *this, this->table().size() ); } \
    friend class StoreIterator< This >

template < typename This >
class StoreIterator
    : std::iterator< std::input_iterator_tag, typename This::Vertex > {
    using Vertex = typename This::Vertex;
    size_t i;
    This& store;

    void bump() {
        while ( i < store.table().size()
                && !store.hasher().valid( store.table()[ i ] ) )
            ++i;
    }

  public:
    StoreIterator( This& store, size_t i ) : i( i ), store( store )
    {
        bump();
    }

    Vertex operator*() {
        return store.vertex( store.table()[ i ] );
    }

    StoreIterator& operator++() {
        ++i;
        bump();
        return *this;
    }

    bool operator==( StoreIterator b ) {
        return i == b.i;
    }

    bool operator!=( StoreIterator b ) {
        return i != b.i;
    }
};

template< typename Store >
struct _Vertex
{
    using Handle = typename Store::Handle;
    using Node = typename Store::Node;

    template< typename T >
    T &extension( int offset = 0 ) const {
        assert( _s );
        return *reinterpret_cast< T * >( _s->extension( _h ) + offset );
    }

    Node node() const {
        if ( _s && !_s->valid( _n ) && _s->valid( _h ) )
            _n = _s->unpack( _h );
        return _n;
    }

    void disown() { _n = typename Store::Node(); }
    Handle handle() { return _h; }

    _Vertex() : _s( nullptr ) {}
    _Vertex( const _Vertex &v ) : _s( v._s ), _h( v._h ) {}
    _Vertex( Store &s, Handle h )
        : _s( &s ), _h( h )
    {}
    ~_Vertex() {
        if ( _s )
            _s->free_unpacked( _n );
    }

private:
    Store *_s;
    typename Store::Handle _h;
    mutable typename Store::Node _n;
};

struct TrivialHandle {
    Blob b;
    TrivialHandle() = default;
    explicit TrivialHandle( Blob b ) : b( b ) {}
    uint64_t asNumber() { return b.raw(); }
};

template< typename BS >
typename BS::bitstream &operator<<( BS &bs, TrivialHandle h )
{
    return bs << h.b.raw();
}

template< typename BS >
typename BS::bitstream &operator>>( BS &bs, TrivialHandle &h )
{
    uint64_t n;
    bs >> n;
    h.b = Blob::fromRaw( n );
    return bs;
}

template < typename TableProvider,
           typename _Generator, typename _Hasher, typename Statistics >
struct DefaultStore
    : StoreCommon< PlainTable< TableProvider, _Generator, _Hasher >,
                   Statistics >
{
    using This = DefaultStore< TableProvider, _Generator, _Hasher, Statistics >;
    using Base = StoreCommon< PlainTable< TableProvider, _Generator, _Hasher >, Statistics >;
    using Hasher = _Hasher;
    using Table = typename Base::Table;

    using Node = typename Base::InsertItem;
    using Vertex = _Vertex< This >;
    using Handle = TrivialHandle;

    void free_unpacked( Node n ) {}
    void free( Node n ) { this->pool().free( n ); }

    bool valid( Node n ) { return Base::valid( n ); }
    bool valid( Handle h ) { return this->pool().valid( h.b ); }
    bool valid( Vertex v ) { return valid( v.handle() ); }
    bool equal( Handle a, Handle b ) { return a.b.raw() == b.b.raw(); }
    bool equal( Node a, Node b ) { return Base::equal( a, b ); }

    int owner( Vertex v, hash_t hint = 0 ) { return owner( v.handle(), hint ); }
    int owner( Handle h, hash_t hint = 0 ) { return owner( h.b, hint ); }
    int owner( Node n, hash_t hint = 0 ) { return Base::owner( n, hint ); }

    int knows( Handle h, hash_t hint = 0 ) { return knows( h.b, hint ); }
    int knows( Node n, hash_t hint = 0 ) { return Base::knows( n, hint ); }

    Vertex vertex( Handle h ) { return Vertex( *this, h ); }

    /* Remember the extension for this Handle, but we won't need to get
     * successors from it. A Vertex obtained from a discarded Handle may fail
     * to produce a valid Node. */
    void discard( Handle ) {}

    Blob unpack( Handle h ) { return h.b; }

    IsNew< Vertex > store( Node n, hash_t h = 0 ) {
        int fake;
        return this->_store( n, h, fake ).map(
            [this, &n]( Node x ) {
                if ( x.raw() != n.raw() )
                    this->free( n );
                return Vertex( *this, Handle( x ) );
            } );
    }

    IsNew< Vertex > fetch( Node n, hash_t h = 0 )
    {
        return this->_fetch( n, h ).map(
            [this]( Node x ) { return Vertex( *this, Handle( x ) ); } );
    }

    template< typename T = char >
    T *extension( Handle h ) {
        return reinterpret_cast< T* >( this->pool().dereference( h.b ) );
    }

    template < typename Graph >
    DefaultStore( Graph &g, int slack, This *m = nullptr )
        : Base( g.pool(), Hasher( g.pool(), slack ), m )
    { }

    STORE_ITERATOR;
private:
    Vertex vertex( Node n ) { return Vertex( *this, Handle( n ) ); }
};

#if 0
template< typename Hasher >
struct HcHasher : Hasher
{
    HcHasher( Pool& pool, int slack ) : Hasher( pool, slack )
    { }

    bool equal(Blob a, Blob b) {
        return true;
    }
};

template < template < typename, typename, template <
              template < typename, typename > class,
              typename, typename > class
            > class Utils,
         typename _Generator, typename _Hasher, typename Statistics >
struct HcStore
    : public StoreCommon< HcStore< Utils, _Generator, _Hasher, Statistics >,
                          Utils< _Generator, HcHasher< _Hasher >, TableIdentity >,
                          Statistics >
{
    using TableUtils = Utils< _Generator, HcHasher< _Hasher >, TableIdentity >;
    using This = HcStore< Utils,_Generator, _Hasher, Statistics >;
    using Base = StoreCommon< This, TableUtils, Statistics >;
    STORE_CLASS;
    static_assert( wibble::TSame< Node, Blob >::value,
                   "HcStore can only work with Blob nodes" );

    using VertexId = StdVertexId< Node >;
    using Vertex = CompressedVertex< Node, Node >;
    using QueueVertex = Vertex;

    template < typename Graph >
    HcStore( Graph& g, Hasher h, This *master = nullptr ) :
        Base( master, h )
    {
        static_assert( wibble::TSame< typename Graph::Node, Node >::value,
                "using incompatible graph" );
    }

    template < typename Graph >
    HcStore( Graph& g, int slack, This *m = nullptr ) :
             This( g, Hasher( g.pool(), slack ), m )
    { }

    hash_t& stubHash( Blob stub ) {
        return pool().template get< hash_t >( stub, slack() );
    }

    hash_t stubHash( const Blob stub ) const {
        return this->pool().template get< hash_t >( stub, this->slack() );
    }

    std::tuple< Vertex, bool > fetch( Blob s, hash_t h ) {
        Blob found;
        bool had;
        std::tie( found, had ) = Base::_fetch( s, h );

        if ( had ) {
            // copy saved state information
            pool().copy( found, s, slack() );
            return std::make_tuple( Vertex( s, found ), had );
        }
        return std::make_tuple( Vertex( found, Node() ), had );
    }

    Vertex fetchByVertexId( VertexId ) {
        assert_unimplemented();
    }

    VertexId fetchVertexId( VertexId vi ) {
        return VertexId( std::get< 0 >( Base::_fetch( vi.node, stubHash( vi.node ) ) ) );
    }

    std::tuple< Vertex, bool > store( Blob s, hash_t h ) {
        // store just a stub containing state information
        Blob stub = this->pool().allocate( slack() + int( sizeof( hash_t ) ) );
        this->pool().copy( s, stub, slack() );
        stubHash( stub ) = h;
        Node n;
        bool inserted;
        std::tie( n, inserted ) = Base::_store( stub, h );
        if ( !inserted )
            pool().free( stub );
        assert( equal( s, stub ) );
        return std::make_tuple( Vertex( s, n ), inserted );
    }

    void update( Blob s, hash_t h ) {
        // update state information in hashtable
        Blob stub;
        bool had;
        std::tie( stub, had ) = table().getHinted( s, h );
        assert( valid( stub ) );
        assert( had );
        pool().copy( s, stub, slack() );
    }

    bool has( Node node ) {
        return table().has( node );
    }

    template< typename W >
    int owner( W &w, Node n, hash_t hint = 0 ) const {
        return _owner( w, hint ? hint : hash( n ) );
    }

    template< typename W >
    int owner( W &w, Vertex s, hash_t hint = 0 ) const {
        return _owner( w, stubHash( s.compressed ) );
    }

    template< typename W >
    int owner( W &w, VertexId h ) const {
        return _owner( w, stubHash( h.node ) );
    }

    bool valid( Vertex v ) {
        return valid( v.base.node );
    }

    bool valid( VertexId vi ) {
        return valid( vi.node );
    }

    ptrdiff_t compareId( VertexId a, VertexId b ) {
        return Base::_compareId( []( VertexId x ) { return x.node; }, a, b );
    }

    STORE_ITERATOR;
};
#endif

template < typename TableProvider,
           typename _Generator, typename _Hasher, typename Statistics >
struct NTreeStore
    : StoreCommon< NTreeTable< TableProvider, _Generator, _Hasher >,
                   Statistics >
{
    using This = NTreeStore< TableProvider, _Generator, _Hasher, Statistics >;
    using Base = StoreCommon< NTreeTable< TableProvider, _Generator, _Hasher >, Statistics >;
    using Hasher = _Hasher;
    using Table = typename Base::Table;

    using Node = typename Base::InsertItem;
    using Vertex = _Vertex< This >;
    using Handle = TrivialHandle;

    using Root = typename Table::Root;

    _Generator& generator;

    template< typename Graph >
    NTreeStore( Graph& g, int slack, This *m = nullptr ) :
        Base( g.pool(), Hasher( g.pool(), slack ), m ), generator( g.base() )
    { }

    IsNew< Vertex > store( Node n, hash_t h = 0 ) {
        return this->_store( n, h, generator ).map(
            [this, &n]( Root x ) {
                this->free( n );
                return Vertex( *this, Handle( x.b ) );
            } );
    }

    IsNew< Vertex > fetch( Node n, hash_t h = 0 )
    {
        return this->_fetch( n, h ).map(
            [this]( Root x ) { return Vertex( *this, Handle( x.b ) ); } );
    }

    Blob unpack( Handle h ) { return Root( h.b ).reassemble( this->pool() ); }

    void free_unpacked( Node n ) { this->free( n ); }
    void free( Node n ) { this->pool().free( n ); }

    bool valid( Node n ) { return Base::valid( n ); }
    bool valid( Handle h ) { return this->pool().valid( h.b ); }
    bool valid( Vertex v ) { return valid( v.handle() ); }
    bool equal( Handle a, Handle b ) { return a.b.raw() == b.b.raw(); }
    bool equal( Node a, Node b ) { return Base::equal( a, b ); }

    hash_t hash( Node n ) { return this->hasher().hash( n ); }
    hash_t hash( Handle h ) { return Root( h.b ).hash( this->pool() ); }

    int owner( Vertex v, hash_t hint = 0 ) { return owner( v.handle(), hint ); }
    int owner( Handle h, hash_t hint = 0 ) { return Base::owner( hash( h ) ); }
    int owner( Node n, hash_t hint = 0 ) { return Base::owner( n, hint ); }

    int knows( Handle h, hash_t hint = 0 ) { return Base::knows( hash( h ) ); }
    int knows( Node n, hash_t hint = 0 ) { return Base::knows( n, hint ); }

    Vertex vertex( Handle h ) { return Vertex( *this, h ); }

    template< typename T = char >
    T *extension( Handle h ) {
        return reinterpret_cast< T* >( Root( h.b ).slack( this->pool() ) );
    }

    STORE_ITERATOR;
private:
    Vertex vertex( Node n ) { return Vertex( *this, Handle( n ) ); }
};

}
}
#undef STORE_CLASS
#undef STORE_ITERATOR
#endif
