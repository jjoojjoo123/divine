// -*- C++ -*-
#include <wibble/test.h> // for assert
#include <wibble/sys/mutex.h> // for assert
#include <pthread.h>
#include <map>
#include <deque>
#include <iostream>

#ifndef DIVINE_POOL_H
#define DIVINE_POOL_H

namespace divine {

struct Pool {
    struct Block
    {
        size_t size;
        char *start;
        Block() : size( 0 ), start( 0 ) {}
    };

    struct Group
    {
        size_t item, used, total, peak, freed, allocated, stolen;
        char **free; // reuse allocation
        char *current; // tail allocation
        char *last; // bumper
        std::vector< Block > blocks;
        Group()
            : item( 0 ), used( 0 ), total( 0 ), peak( 0 ),
              freed( 0 ), allocated( 0 ), stolen( 0 ),
              free( 0 ), current( 0 ), last( 0 )
        {}
    };

    typedef std::vector< Group > Groups;
    Groups m_groups;

    Pool();
    Pool( const Pool & );

    size_t peakAllocation() {
        size_t total = 0;
        Groups::iterator i;
        for ( i = m_groups.begin(); i != m_groups.end(); ++i ) {
            total += i->total;
        }
        return total;
    }

    size_t peakUsage() {
        size_t total = 0;
        Groups::iterator i;
        for ( i = m_groups.begin(); i != m_groups.end(); ++i ) {
            total += i->peak;
        }
        return total;
    }

    void newBlock( Group *g )
    {
        Block b;
        size_t s = std::min( 2 * g->blocks.back().size, size_t( 1024 * 1024 ) );
        b.size = s;
        b.start = new char[ s ];
        g->current = b.start;
        g->last = b.start + s;
        g->total += s;
        g->blocks.push_back( b );
    }

    size_t adjustSize( size_t bytes )
    {
        // round up to a value divisible by 4
        bytes += 3;
        bytes = ~bytes;
        bytes |= 3;
        bytes = ~bytes;
        return bytes;
    }

    Group *group( size_t bytes )
    {
        if ( bytes / 4 >= m_groups.size() )
            return 0;
        assert( bytes % 4 == 0 );
        if ( m_groups[ bytes / 4 ].total )
            return &(m_groups[ bytes / 4 ]);
        else
            return 0;
    }

    Group *createGroup( size_t bytes )
    {
        assert( bytes % 4 == 0 );
        Group g;
        g.item = bytes;
        Block b;
        b.size = 1024 * bytes;
        b.start = new char[ b.size ];
        g.blocks.push_back( b );
        g.current = b.start;
        g.last = b.start + b.size;
        g.total = b.size;
        m_groups.resize( std::max( bytes / 4 + 1, m_groups.size() ), Group() );
        assert( m_groups[ bytes / 4 ].total == 0 );
        m_groups[ bytes / 4 ] = g;
        assert( g.total != 0 );
        assert( group( bytes ) );
        assert( group( bytes )->total == g.total );
        // m_groups.insert( std::make_pair( bytes, g ) );
        return group( bytes );
    }

    char *alloc( size_t bytes )
    {
        assert( bytes );
        bytes = adjustSize( bytes );
        char *ret = 0;

        Group *g = group( bytes );
        if (!g)
            g = createGroup( bytes );

        if ( g->free ) {
            ret = reinterpret_cast< char * >( g->free );
            g->free = *reinterpret_cast< char *** >( ret );
        } else {
            if ( g->current + bytes > g->last ) {
                newBlock( g );
            }
            ret = g->current;
            g->current += bytes;
        }
        g->used += bytes;
        g->allocated += bytes;
        if ( g->used > g->peak ) g->peak = g->used;
        assert( g->used <= g->total );

        return ret;
    }

    size_t pointerSize( char *_ptr )
    {
        return 0; // replace with magic
    }

    void release( Group *g, char *ptr, size_t size )
    {
        assert( g );
        assert( g->item == size );
        g->freed += size;
        char ***ptr3 = reinterpret_cast< char *** >( ptr );
        char **ptr2 = reinterpret_cast< char ** >( ptr );
        *ptr3 = g->free;
        g->free = ptr2;
    }

    template< typename T >
    void free( T *_ptr, size_t size = 0 )
    {   // O(1) free
        if ( !_ptr ) return; // noop
        char *ptr = reinterpret_cast< char * >( _ptr );
        if ( !size )
            size = pointerSize( ptr );
        else {
            assert( size );
            size = adjustSize( size );
        }
        assert( size );
        Group *g = group( size );
        release( g, ptr, size );
        assert( g->used >= size );
        g->used -= size;
    }

    template< typename T >
    void steal( T *_ptr, size_t size = 0 )
    {
        char *ptr = reinterpret_cast< char * >( _ptr );
        if ( !size )
            size = pointerSize( ptr );
        else {
            assert( size );
            size = adjustSize( size );
        }
        assert( size );
        Group *g = group( size );
        if ( !g )
            g = createGroup( size );
        assert( g );
        g->total += size;
        g->stolen += size;
        release( g, ptr, size );
    }

    std::ostream &printStatistics( std::ostream &s ) {
        for ( Groups::iterator i = m_groups.begin(); i != m_groups.end(); ++i ) {
            if ( i->total == 0 )
                continue;
            s << "group " << i->item
              << " holds " << i->used
              << " (peaked " << i->peak
              << "), allocated " << i->allocated
              << " and freed " << i->freed << " bytes in "
              << i->blocks.size() << " blocks"
              << std::endl;
        }
        return s;
    };
};

struct ThreadPoolManager {
    static pthread_key_t s_pool_key;
    static pthread_once_t s_pool_once;
    typedef std::deque< Pool * > Available;
    static Available *s_available;
    static wibble::sys::Mutex *s_mutex;

    static wibble::sys::Mutex &mutex() {
        if ( !s_mutex )
            s_mutex = new wibble::sys::Mutex();
        return *s_mutex;
    }

    static Available &available() {
        if ( !s_available )
            s_available = new Available();
        return *s_available;
    }

    static void pool_key_alloc() {
        pthread_key_create(&s_pool_key, pool_key_reclaim);
    }

    static void pool_key_reclaim( void *p ) {
        wibble::sys::MutexLock __l( mutex() );
        available().push_back( static_cast< Pool * >( p ) );
    }

    static void add( Pool *p ) {
        wibble::sys::MutexLock __l( mutex() );
        pthread_once( &s_pool_once, pool_key_alloc );
        available().push_back( p );
    }

    static Pool *force( Pool *p ) {
        wibble::sys::MutexLock __l( mutex() );
        Pool *current =
            static_cast< Pool * >( pthread_getspecific( s_pool_key ) );
        if ( current == p )
            return p;
        Available::iterator i =
            std::find( available().begin(), available().end(), p );
        assert( i != available().end() );
        Pool *p1 = *i, *p2 =
                   static_cast< Pool * >( pthread_getspecific( s_pool_key ) );
        available().erase( i );
        if ( p2 )
            available().push_back( p2 );
        pthread_setspecific( s_pool_key, p1 );
        return p1;
    }

    static Pool *get() {
        std::cerr << "querying" << std::endl;
        Pool *p = static_cast< Pool * >( pthread_getspecific( s_pool_key ) );
        if ( !p ) {
            wibble::sys::MutexLock __l( mutex() );
            if ( available().empty() )
                p = new Pool();
            else {
                p = available().front();
                available().pop_front();
            }
            pthread_setspecific( s_pool_key, p );
        }
        return p;
    }
};

template< typename T >
class Allocator
{
public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef T*         pointer;
    typedef const T*   const_pointer;
    typedef T&         reference;
    typedef const T&   const_reference;
    typedef T          value_type;

    template<typename T1>
    struct rebind
    { typedef Allocator<T1> other; };

    Pool *m_pool;

    Allocator() throw() {
        m_pool = ThreadPoolManager::get();
    }

    Allocator(const Allocator&) throw() {}

      template<typename T1>
      Allocator(const Allocator<T1> &) throw() {}

      ~Allocator() throw() {}

      pointer address(reference x) const { return &x; }
      const_pointer address(const_reference x) const { return &x; }

      // NB: __n is permitted to be 0.  The C++ standard says nothing
      // about what the return value is when __n == 0.
      pointer allocate( size_type n, const void* = 0 )
      { 
      }

      // __p is not permitted to be a null pointer.
      void deallocate(pointer __p, size_type) {}

#if 0
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 402. wrong new expression in [some_] allocator::construct
      void 
      construct(pointer __p, const _Tp& __val) 
      { ::new((void *)__p) _Tp(__val); }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
      template<typename... _Args>
        void
        construct(pointer __p, _Args&&... __args)
	{ ::new((void *)__p) _Tp(std::forward<_Args>(__args)...); }
#endif

      void 
      destroy(pointer __p) { __p->~_Tp(); }
#endif
    };

template<typename T>
inline bool operator==(const Allocator<T> &a, const Allocator<T> &b)
{
    return a.m_pool == b.m_pool;
}
  
template<typename T>
inline bool operator!=(const Allocator<T> &a, const Allocator<T> &b)
{
    return a.m_pool != b.m_pool;
}

}

namespace std {

inline void swap( divine::Pool &a, divine::Pool &b )
{
    swap( a.m_groups, b.m_groups );
}

}

#endif
