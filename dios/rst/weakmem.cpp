// -*- C++ -*- (c) 2015-2018 Vladimír Štill <xstill@fi.muni.cz>

#include <algorithm> // reverse iterator
#include <cstdarg>
#include <dios.h>
#include <util/map.hpp>
#include <sys/interrupt.h>
#include <sys/lart.h>
#include <sys/vmutil.h>
#include <sys/cdefs.h>
#include <dios/sys/kernel.hpp> // get_debug
#include <rst/common.h> // weaken

#define _WM_INLINE __attribute__((__always_inline__, __flatten__))
#define _WM_NOINLINE __attribute__((__noinline__))
#define _WM_NOINLINE_WEAK __attribute__((__noinline__, __weak__))
#define _WM_INTERFACE __attribute__((__nothrow__, __noinline__, __flatten__)) __invisible extern "C"

namespace lart::weakmem {

struct CasRes { uint64_t value; bool success; };

_WM_INTERFACE int __lart_weakmem_buffer_size() noexcept;
_WM_INTERFACE int __lart_weakmem_min_ordering() noexcept;

_WM_INLINE
static char *baseptr( char *ptr ) noexcept {
    uintptr_t base = uintptr_t( ptr ) & ~_VM_PM_Off;
    uintptr_t type = (base & _VM_PM_Type) >> _VM_PB_Off;
    if ( type == _VM_PT_Marked || type == _VM_PT_Weak )
        base = (base & ~_VM_PM_Type) | (_VM_PT_Heap << _VM_PB_Off);
    return reinterpret_cast< char * >( base );
}

using MaskFlags = uint64_t;

static const MaskFlags setFlags = _DiOS_CF_Mask
                                | _LART_CF_RelaxedMemRuntime
                                | _VM_CF_IgnoreLoop
                                | _VM_CF_IgnoreCrit;
static const MaskFlags restoreFlags = setFlags | _DiOS_CF_Deferred;


_WM_INTERFACE MaskFlags __lart_weakmem_mask_enter() noexcept {
    MaskFlags restore = MaskFlags( __vm_ctl_get( _VM_CR_Flags ) );
    __vm_ctl_flag( /* clear: */ _DiOS_CF_Deferred, setFlags );
    return restore;
}

_WM_INTERFACE void __lart_weakmem_mask_leave( MaskFlags restore ) noexcept {
    restore |= MaskFlags( __vm_ctl_get( _VM_CR_Flags ) ) & _DiOS_CF_Deferred; // keep deferred if set now
    __vm_ctl_flag( /* clear: */ ((~restore) & restoreFlags),
                   /* set: */ (restore & restoreFlags) );
    // if we are unmasking now, and there is deferred interrupt, perform it
    if ( (restore & _DiOS_CF_Mask) == 0 && (restore & _DiOS_CF_Deferred) != 0 )
        __dios_interrupt();
}

_WM_INLINE bool _bypass( MaskFlags restore ) noexcept {
    return restore & (_VM_CF_DebugMode | _LART_CF_RelaxedMemRuntime);
}

_WM_INLINE bool _kernel( MaskFlags restore ) noexcept {
    return restore & _VM_CF_KernelMode;
}

struct FullMask {
    _WM_INLINE FullMask() noexcept : restore( __lart_weakmem_mask_enter() ) { }
    _WM_INLINE ~FullMask() noexcept { __lart_weakmem_mask_leave( restore ); }
    _WM_INLINE bool bypass() const noexcept { return _bypass( restore ); }
    _WM_INLINE bool kernel() const noexcept { return _kernel( restore ); }

  private:
    MaskFlags restore;
};

struct ExternalMask {
    _WM_INLINE ExternalMask( MaskFlags restore ) noexcept : restore( restore ) { }
    _WM_INLINE ~ExternalMask() noexcept {
        // allow the __vm_test_crit after the op to fire
        __vm_ctl_flag( (~restore) & _VM_CF_IgnoreCrit, 0 );
    }
    _WM_INLINE bool bypass() const noexcept { return _bypass( restore ); }
    _WM_INLINE bool kernel() const noexcept { return _kernel( restore ); }

  private:
    MaskFlags restore;
};

struct UnlockCrit {
    _WM_INLINE UnlockCrit() noexcept { __vm_ctl_flag( _VM_CF_IgnoreCrit, 0 ); }
    _WM_INLINE ~UnlockCrit() noexcept { __vm_ctl_flag( 0, _VM_CF_IgnoreCrit ); }
};

template< typename T >
using ThreadMap = __dios::ArrayMap< __dios_task, T >;

template< typename T >
using Array = __dios::Array< T >;

template< typename It >
struct Range {
    using T = typename It::value_type;
    using iterator = It;
    using const_iterator = It;

    Range( It begin, It end ) noexcept : _begin( begin ), _end( end ) { }
    Range( const Range & ) noexcept = default;

    _WM_INLINE
    iterator begin() const noexcept { return _begin; }

    _WM_INLINE
    iterator end() const noexcept { return _end; }

  private:
    It _begin, _end;
};

template< typename It >
_WM_INLINE
static Range< It > range( It begin, It end ) noexcept { return Range< It >( begin, end ); }

template< typename T >
_WM_INLINE
static auto reversed( T &x ) noexcept { return range( x.rbegin(), x.rend() ); }

_WM_INLINE
static const char *ordstr( MemoryOrder mo ) {
    if ( subseteq( MemoryOrder::SeqCst, mo ) )
        return "SC";
    if ( subseteq( MemoryOrder::AcqRel, mo ) )
        return "AR";
    if ( subseteq( MemoryOrder::Acquire, mo ) )
        return "Acq";
    if ( subseteq( MemoryOrder::Release, mo ) )
        return "Rel";
    if ( subseteq( MemoryOrder::Monotonic, mo ) )
        return "Mon";
    if ( subseteq( MemoryOrder::Unordered, mo ) )
        return "U";
    return "N";
}

__weakmem_direct _WM_NOINLINE_WEAK
uint64_t _load( char *addr, uint32_t bitwidth ) noexcept {
    UnlockCrit _;
    switch ( bitwidth ) {
        case 1: case 8: return *reinterpret_cast< uint8_t * >( addr );
        case 16: return *reinterpret_cast< uint16_t * >( addr );
        case 32: return *reinterpret_cast< uint32_t * >( addr );
        case 64: return *reinterpret_cast< uint64_t * >( addr );
        default: __dios_fault( _VM_F_Control, "Unhandled case" );
    }
    return 0;
}

__weakmem_direct _WM_NOINLINE_WEAK
void _store( char *addr, uint64_t value, int bitwidth ) noexcept {
    UnlockCrit _;
    switch ( bitwidth ) {
        case 1: case 8: *reinterpret_cast< uint8_t * >( addr ) = value; break;
        case 16:       *reinterpret_cast< uint16_t * >( addr ) = value; break;
        case 32:       *reinterpret_cast< uint32_t * >( addr ) = value; break;
        case 64:       *reinterpret_cast< uint64_t * >( addr ) = value; break;
        case 0: break; // fence
        default: __dios_fault( _VM_F_Control, "Unhandled case" );
    }
}

struct BufferLine : brick::types::Ord {

    BufferLine() noexcept = default;
    BufferLine( MemoryOrder order ) noexcept : order( order ) { } // fence
    BufferLine( char *addr, uint64_t value, uint32_t bitwidth, MemoryOrder order ) noexcept :
        addr( addr ), value( value ), bitwidth( bitwidth ), order( order )
    {
        assert( addr != abstract::weaken( addr ) );
    }

    _WM_INLINE
    bool isFence() const noexcept { return !addr; }

    _WM_INLINE
    bool isStore() const noexcept { return addr; }

    _WM_INLINE
    void store() noexcept { _store( addr, value, bitwidth ); }

    _WM_INLINE
    bool matches( const char *const from, const char *const to ) const noexcept {
        return (from <= addr && addr < to) || (addr <= from && from < addr + bitwidth / 8);
    }

    _WM_INLINE
    bool matches( const char *const mem, const uint32_t size ) const noexcept {
        return matches( mem, mem + size );
    }

    _WM_INLINE
    bool matches( BufferLine &o ) noexcept {
        return matches( o.addr, o.bitwidth / 8 );
    }

    enum class Status : int8_t {
        Normal, Committed
    };

    char *addr = nullptr;
    uint64_t value = 0;

    uint8_t bitwidth = 0;
    MemoryOrder order = MemoryOrder::NotAtomic;
    int16_t sc_seq = 0; // note: we can go negative in the flushing process
    int16_t at_seq = 0; // same here
    Status status = Status::Normal;

    _WM_INLINE
    auto as_tuple() const noexcept {
        // note: ignore status in comparison, take into account only part
        // relevant to memory behaviour
        return std::tie( addr, value, bitwidth, order, sc_seq, at_seq );
    }

    _WM_INLINE
    void dump() const noexcept {
        char buffer[] = "[0xdeadbeafdeadbeaf ← 0xdeadbeafdeadbeaf; 00 bit; CWA SC;000;000]";
        snprintf( buffer, sizeof( buffer ) - 1, "[0x%llx ← 0x%llx; %d bit; %c%c%c%s;%d;%d]",
                  uint64_t( addr ), value, bitwidth,
                  status == Status::Committed ? 'C' : ' ',
                  subseteq( MemoryOrder::WeakCAS, order ) ? 'W' : ' ',
                  subseteq( MemoryOrder::AtomicOp, order ) ? 'A' : ' ',
                  ordstr( order ), at_seq, sc_seq );
        __vm_trace( _VM_T_Text, buffer );
    }
};

using Status = BufferLine::Status;

static_assert( sizeof( BufferLine ) == 3 * sizeof( uint64_t ) );

struct Buffer : Array< BufferLine >
{
    Buffer() = default;

    _WM_INLINE
    int storeCount() noexcept {
        return std::count_if( begin(), end(), []( BufferLine &l ) noexcept { return l.isStore(); } );
    }

    _WM_INLINE
    BufferLine &newest() noexcept { return end()[ -1 ]; }

    _WM_INLINE
    BufferLine &oldest() noexcept { return *begin(); }

    _WM_INLINE
    void erase( const int i ) noexcept {
        erase( begin() + i );
    }

    _WM_INLINE
    void erase( iterator i ) noexcept {
        std::move( i + 1, end(), i );
        pop_back();
    }

    _WM_INLINE
    void evict( char *const ptr ) noexcept {
        const auto id = baseptr( ptr );
        evict( [id]( BufferLine &l ) noexcept { return baseptr( l.addr ) != id; } );
    }

    _WM_INLINE
    void evict( char *from, char *to ) noexcept {
        evict( [from, to]( BufferLine &l ) noexcept {
                return !( l.addr >= from && l.addr + l.bitwidth / 8 <= to );
            } );
    }

    template< typename Filter >
    _WM_INLINE
    void evict( Filter filter ) noexcept {
        int cnt = std::count_if( begin(), end(), filter );
        if ( cnt == 0 )
            clear();
        else if ( cnt < size() )
        {
            auto dst = begin();
            for ( auto &l : *this ) {
                if ( filter( l ) ) {
                    if ( dst != &l )
                        *dst = std::move( l );
                    ++dst;
                }
            }
        }
        _resize( cnt );
    }

    _WM_INLINE
    void shrink( size_t n ) noexcept {
        std::destroy( begin() + n, end() );
        _resize( n );
    }

    _WM_INLINE
    void cleanOldAndFlushed() noexcept {
        while ( size() > 0 && oldest().isFence() )
            erase( 0 );
    }

    _WM_INLINE
    void dump() const noexcept {
        for ( auto &e : *this )
            e.dump();
    }
};

struct Buffers : ThreadMap< Buffer > {

    using Super = ThreadMap< Buffer >;

    _WM_INLINE
    Buffer *getIfExists( __dios_task h ) noexcept {
        auto it = find( h );
        return it != end() ? &it->second : nullptr;
    }

    _WM_INLINE
    Buffer &get( __dios_task tid ) noexcept {
        Buffer *b = getIfExists( tid );
        if ( !b ) {
            b = &emplace( tid, Buffer{} ).first->second;
        }
        return *b;
    }

    _WM_INLINE
    Buffer *getIfExists() noexcept { return getIfExists( __dios_this_task() ); }

    _WM_INLINE
    Buffer &get() noexcept { return get( __dios_this_task() ); }

    template< typename T, T v >
    struct Const {
        template< typename... Args >
        _WM_INLINE
        T operator()( Args &&... ) const noexcept { return v; }
    };

    template< typename Self, typename Yield, typename Filter >
    _WM_INLINE
    static void _for_each( Self &self, Yield yield, Filter filt ) noexcept
    {
        for ( auto &p : self ) {
            if ( filt( p.first ) )
                for ( auto &l : p.second )
                    yield( p.first, p.second, l );
        }
    }

    template< typename Yield, typename Filter = Const< bool, true > >
    _WM_INLINE
    void for_each( Yield y, Filter filt = Filter() ) noexcept {
        _for_each( *this, y, filt );
    }

    template< typename Yield, typename Filter = Const< bool, true > >
    _WM_INLINE
    void for_each( Yield y, Filter filt = Filter() ) const noexcept {
        _for_each( *this, y, filt );
    }

    template< typename Filter = Const< bool, true > >
    _WM_INLINE
    int16_t get_next_sc_seq( Filter filt = Filter() ) const noexcept
    {
        int16_t max = 0;
        for_each( [&]( auto &, auto &, auto &l ) noexcept {
                max = std::max( max, l.sc_seq );
            }, filt );
        return max + 1;
    }

    template< typename Filter = Const< bool, true > >
    _WM_INLINE
    int16_t get_next_at_seq( char *addr, int bitwidth, Filter filt = Filter() ) const noexcept
    {
        int16_t max = 0;
        for_each( [&]( auto &, auto &, auto &l ) noexcept {
                if ( l.addr == addr && l.bitwidth == bitwidth ) {
                    if ( l.at_seq > max ) {
                        max = l.at_seq;
                    }
                } else if ( l.matches( addr, bitwidth / 8 ) ) {
                    __dios_fault( _VM_F_Memory, "Atomic operations over overlapping, "
                            "but not same memory locations are not allowed" );
                }
            }, filt );
        return max + 1;
    }

    _WM_INLINE
    void fix_at_seq( BufferLine &entry ) noexcept
    {
        for_each( [&]( auto &, auto &, auto &l ) noexcept {
                if ( l.addr == entry.addr && l.bitwidth == entry.bitwidth && l.at_seq ) {
                    l.at_seq -= entry.at_seq;
                }
            } );
    }

    _WM_INLINE
    void fix_sc_seq( BufferLine &entry ) noexcept
    {
        for_each( [&]( auto &, auto &, auto &l ) noexcept {
                if ( l.sc_seq ) {
                    l.sc_seq -= entry.sc_seq;
                }
            } );
    }

    _WM_INLINE
    void push( __dios_task tid, Buffer &b, BufferLine &&line ) noexcept
    {
        if ( subseteq( MemoryOrder::AtomicOp, line.order ) )
            line.at_seq = get_next_at_seq( line.addr, line.bitwidth );
        if ( subseteq( MemoryOrder::SeqCst, line.order ) )
            line.sc_seq = get_next_sc_seq();
        push_tso( tid, b, std::move( line ) );
    }

    _WM_INLINE
    void push_tso( __dios_task tid, Buffer &b, BufferLine &&line ) noexcept
    {
        b.push_back( std::move( line ) );

        // there can be fence as oldest entry, so we need while here
        while ( b.storeCount() > __lart_weakmem_buffer_size() ) {
            auto &oldest = b.oldest();
            if ( oldest.isStore() ) {
                tso_load< true >( oldest.addr, oldest.bitwidth, tid );
                oldest.store();
            }
            b.erase( 0 );
            b.cleanOldAndFlushed();
        }
    }

    _WM_INLINE
    void dump() noexcept
    {
        auto *hids = __dios::have_debug() ? &__dios::get_debug().hids : nullptr;
        for ( auto &p : *this ) {
            if ( !p.second.empty() ) {
                auto tid = abstract::weaken( p.first );
                int nice_id = [&]() noexcept { if ( !hids ) return -1;
                                    auto it = hids->find( tid );
                                    return it == hids->end() ? -1 : it->second;
                                  }();

                char buffer[] = "thread 0xdeadbeafdeadbeaf*: ";
                snprintf( buffer, sizeof( buffer ) - 1, "thread: %d%s ",
                                  nice_id,
                                  p.first == __dios_this_task() ? "*:" : ": " );
                __vm_trace( _VM_T_Text, buffer );
                p.second.dump();
            }
        }
    }

    _WM_INLINE
    void flush( __dios_task tid, Buffer &buf ) noexcept
    {
        for ( auto &l : buf ) {
            if ( l.isStore() ) {
                tso_load< true >( l.addr, l.bitwidth, tid );
                l.store();
            }
        }
        buf.clear();
    }

    _WM_INLINE
    void flush( Buffer &buf, BufferLine *entry, char *addr, int bitwidth ) noexcept {
        auto to_move = buf.begin();
        int kept = 0;
        auto move = [&to_move]( BufferLine &e ) {
            if ( &e != to_move )
                *to_move = e;
            ++to_move;
        };
        for ( auto &e : buf )
        {
            if ( &e > entry ) {
                move( e );
                ++kept;
                continue;
            }
            if ( e.matches( addr, bitwidth / 8 ) ) {
                e.store();
                continue;
            }
            e.status = Status::Committed;
            ++kept;
            move( e );
        }
        if ( kept == 0 )
            buf.clear();
        else
            buf.shrink( kept );
    }

    template< bool skip_local = false >
    _WM_INLINE
    void tso_load( char *addr, int bitwidth, __dios_task tid ) noexcept
    {
        const int sz = size();
        bool dirty = false;
        int todo[sz];
        bool has_committed[sz];
        int needs_commit = 0;
        int i = 0, choices = 1;
        for ( auto &p : *this ) {
            todo[i] = 1;
            has_committed[i] = false;
            const bool nonloc = p.first != tid;
            if ( skip_local && !nonloc )
                goto next;
            for ( auto &e : p.second ) {
                // TODO: flusing ours can be avoided if we don't flush anyone else's too
                if ( e.matches( addr, bitwidth / 8 ) ) {
                    dirty = dirty || nonloc;
                    if ( !skip_local && e.status == Status::Committed ) {
                        has_committed[i] = true;
                        ++needs_commit;
                    }
                    else
                        todo[i]++;
                }
            }
            choices *= todo[i];
          next:
            ++i;
        }
        if ( !dirty )
            return;

        int c = __vm_choose( choices );
        if ( needs_commit == 0 && c == 0 ) // shortcut if we are not flushing anything
            return;

        int cnt = 0;
        for ( int i = 0; i < sz; ++i ) {
            const int m = todo[i];
            cnt += (todo[i] = c % m) != 0 || has_committed[i];
            c /= m;
        }

        auto flush_buf = [&]( Buffer &buf, int last_flushed_idx ) noexcept {
            int lineid = 0;
            BufferLine *line = nullptr;
            for ( auto it = buf.begin(); it != buf.end(); ++it, ++lineid ) {
                if ( it->matches( addr, bitwidth / 8 ) )
                    line = it;
                else
                    continue;
                if ( it->status == Status::Committed )
                    continue;
                if ( lineid == last_flushed_idx )
                    break;
            }
            assert( line );
            flush( buf, line, addr, bitwidth );
        };

        // TODO: partial stores
        int last_id = __vm_choose( cnt );
        Buffer *last = nullptr;
        int last_flushed_idx_of_last = 0;

        for ( int i = 0, id = 0; i < sz; ++i ) {
            if ( todo[i] == 0 && !has_committed[i] )
                continue;
            auto &buf = begin()[i].second;
            if ( last_id == id ) {
                last = &buf;
                last_flushed_idx_of_last = todo[i];
                continue;
            }
            flush_buf( buf, todo[i] );
            ++id;
        }
        assert( last );
        flush_buf( *last, last_flushed_idx_of_last );
    }
};

// avoid global ctors/dtors for Buffers
union BFH {
    BFH() noexcept : raw() { }
    ~BFH() { }
    void *raw;
    Buffers b;
} storeBuffers;

/* weak here is to prevent optimizer from eliminating calls to these functions
 * -- they will be replaced by weakmem transformation */
_WM_NOINLINE_WEAK extern "C" int __lart_weakmem_buffer_size() noexcept { return 2; }
_WM_NOINLINE_WEAK extern "C" int __lart_weakmem_min_ordering() noexcept { return 0; }

_WM_NOINLINE_WEAK __attribute__((__annotate__("divine.debugfn")))
extern "C" void __lart_weakmem_dump() noexcept
{
    storeBuffers.b.dump();
}

_WM_NOINLINE_WEAK
extern "C" void __lart_weakmem_debug_fence() noexcept
{
    // This fence is called only at the beginning of *debugfn* functions,
    // it does not need to keep store buffers consistent, debug functions will
    // not see them, it just needs to make all entries of this thread visible.
    // Also, it is uninterruptible by being part of *debugfn*
    auto *tid = __dios_this_task();
    if ( !tid )
        return;
    auto *buf = storeBuffers.b.getIfExists( tid );
    if ( !buf )
        return;
    for ( auto &l : *buf )
        l.store();
    buf->clear();
}

_WM_INLINE
void __lart_weakmem_store( char *addr, uint64_t value, uint32_t bitwidth,
                           MemoryOrder ord, const ExternalMask &mask )
                           noexcept
{
    if ( !addr )
        __dios_fault( _VM_F_Memory, "weakmem.store: invalid address" );
    if ( bitwidth <= 0 || bitwidth > 64 )
        __dios_fault( _VM_F_Memory, "weakmem.store: invalid bitwidth" );

    BufferLine line{ addr, value, bitwidth, ord };

    if ( mask.bypass() || mask.kernel() ) {
        line.store();
        return;
    }
    auto tid = __dios_this_task();
    auto &buf = storeBuffers.b.get( tid );
    if ( subseteq( MemoryOrder::SeqCst, ord ) ) {
        storeBuffers.b.flush( tid, buf );
        line.store();
    }
    else
        storeBuffers.b.push_tso( tid, buf, std::move( line ) );
}

_WM_INTERFACE
void __lart_weakmem_store( char *addr, uint64_t value, uint32_t bitwidth,
                           MemoryOrder ord, MaskFlags mask )
                           noexcept
{
    __lart_weakmem_store( addr, value, bitwidth, ord, ExternalMask( mask ) );
}

_WM_INTERFACE
void __lart_weakmem_fence( MemoryOrder ord ) noexcept {
    FullMask mask;
    if ( mask.bypass() || mask.kernel() )
        return; // should not be called recursivelly

    auto tid = __dios_this_task();
    auto *buf = storeBuffers.b.getIfExists( tid );
    if ( !buf )
        return;

    if ( subseteq( MemoryOrder::SeqCst, ord ) )
        storeBuffers.b.flush( tid, *buf );
}

union I64b {
    uint64_t i64;
    char b[8];
};

_WM_INLINE
static uint64_t doLoad( Buffer *buf, char *addr, uint32_t bitwidth ) noexcept
{
    I64b val = { .i64 = 0 };
    // always attempt load from memory to check for invalidated memory and idicate
    // load to tau reduction
    I64b mval = { .i64 = _load( addr, bitwidth ) };
    bool bmask[8] = { false };
    bool any = false;
    if ( buf ) {

        for ( const auto &it : reversed( *buf ) ) { // go from newest entry

            if ( !any && it.addr == addr && it.bitwidth >= bitwidth ) {

                return it.value & (uint64_t(-1) >> (64 - bitwidth));
            }
            if ( it.matches( addr, bitwidth / 8 ) ) {
                I64b bval = { .i64 = it.value };
                const int shift = intptr_t( it.addr ) - intptr_t( addr );
                if ( shift >= 0 ) {
                    for ( unsigned i = shift, j = 0; i < bitwidth / 8 && j < it.bitwidth / 8; ++i, ++j )
                        if ( !bmask[ i ] ) {
                            val.b[ i ] = bval.b[ j ];
                            bmask[ i ] = true;
                            any = true;
                        }
                } else {
                    for ( unsigned i = 0, j = -shift; i < bitwidth / 8 && j < it.bitwidth / 8; ++i, ++j )
                        if ( !bmask[ i ] ) {
                            val.b[ i ] = bval.b[ j ];
                            bmask[ i ] = true;
                            any = true;
                        }
                }
            }
        }
    }
    if ( any ) {
        for ( unsigned i = 0; i < bitwidth / 8; ++i )
            if ( !bmask[ i ] )
                val.b[ i ] = mval.b[ i ];
        return val.i64;
    }
    return mval.i64;
}

_WM_INLINE
uint64_t __lart_weakmem_load( char *addr, uint32_t bitwidth, MemoryOrder,
                              const ExternalMask &mask )
                              noexcept
{
    if ( !addr )
        __dios_fault( _VM_F_Memory, "weakmem.load: invalid address" );
    if ( bitwidth <= 0 || bitwidth > 64 )
        __dios_fault( _VM_F_Control, "weakmem.load: invalid bitwidth" );

    if ( mask.bypass() || mask.kernel()  )
        return _load( addr, bitwidth );

    auto tid = __dios_this_task();
    Buffer *buf = storeBuffers.b.getIfExists( tid );

    if ( storeBuffers.b.size() != 1
         || storeBuffers.b.begin()->first != tid )
        storeBuffers.b.tso_load( addr, bitwidth, tid );

    return doLoad( buf, addr, bitwidth );
}

_WM_INTERFACE
uint64_t __lart_weakmem_load( char *addr, uint32_t bitwidth, MemoryOrder mo,
                              MaskFlags mask )
                              noexcept
{
    return __lart_weakmem_load( addr, bitwidth, mo, ExternalMask( mask ) );
}

_WM_INTERFACE
CasRes __lart_weakmem_cas( char *addr, uint64_t expected, uint64_t value, uint32_t bitwidth,
                           MemoryOrder ordSucc, MemoryOrder ordFail, MaskFlags _mask ) noexcept
{
    ExternalMask mask( _mask );
    auto loaded = __lart_weakmem_load( addr, bitwidth, ordFail, mask );

    if ( loaded != expected
            || ( subseteq( MemoryOrder::WeakCAS, ordFail ) && __vm_choose( 2 ) ) )
        return { loaded, false };

    // TODO: when implementing NSW, make sure we order properly with _ordSucc

    __lart_weakmem_store( addr, value, bitwidth, ordSucc, mask );
    return { value, true };
}

_WM_INTERFACE
void __lart_weakmem_cleanup( int32_t cnt, ... ) noexcept {
    FullMask mask;
    if ( mask.bypass() || mask.kernel() )
        return; // should not be called recursivelly

    if ( cnt <= 0 )
        __dios_fault( _VM_F_Control, "invalid cleanup count" );

    va_list ptrs;
    va_start( ptrs, cnt );

    Buffer *buf = storeBuffers.b.getIfExists();
    if ( !buf )
        return;

    for ( int i = 0; i < cnt; ++i ) {
        char *ptr = va_arg( ptrs, char * );
        if ( !ptr )
            continue;
        buf->evict( ptr );
    }
    va_end( ptrs );
}

_WM_INTERFACE
void __lart_weakmem_resize( char *ptr, uint32_t newsize ) noexcept {
    FullMask mask;
    if ( mask.bypass() || mask.kernel() )
        return; // should not be called recursivelly
    uint32_t orig = __vm_obj_size( ptr );
    if ( orig <= newsize )
        return;

    Buffer *buf = storeBuffers.b.getIfExists();
    if ( !buf )
        return;
    auto base = baseptr( ptr );
    buf->evict( base + newsize, base + orig );
}

} // namespace __lart::weakmem
