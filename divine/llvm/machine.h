// -*- C++ -*- (c) 2011, 2012 Petr Rockai

#include <divine/llvm/program.h>
#include <divine/toolkit/lens.h>
#include <divine/graph/allocator.h>

#ifndef DIVINE_LLVM_MACHINE_H
#define DIVINE_LLVM_MACHINE_H

namespace divine {
namespace llvm {

struct Canonic;

struct MachineState
{
    struct StateAddress : lens::LinearAddress
    {
        ProgramInfo *_info;

        StateAddress( StateAddress base, int index, int offset )
            : LinearAddress( base, index, offset ), _info( base._info )
        {}

        StateAddress( ProgramInfo *i, Blob b, int offset )
            : LinearAddress( b, offset ), _info( i )
        {}

        StateAddress copy( StateAddress to, int size ) {
            std::copy( dereference(), dereference() + size, to.dereference() );
            return StateAddress( _info, to.b, to.offset + size );
        }
    };

    struct Frame {
        PC pc;
        char memory[0];

        int framesize( ProgramInfo &i ) {
            return i.function( pc ).framesize;
        }

        StateAddress advance( StateAddress a, int ) {
            return StateAddress( a, 0, sizeof( Frame ) + framesize( *a._info ) );
        }
        char *dereference( ProgramInfo &i, ProgramInfo::Value v ) {
            assert_leq( v.offset, framesize( i ) );
            assert_leq( v.offset + v.width, framesize( i ) );
            return memory + v.offset;
        }
        int end() { return 0; }
    };

    struct Globals {
        char memory[0];
        StateAddress advance( StateAddress a, int ) {
            return StateAddress( a, 0, a._info->globalsize );
        }
        int end() { return 0; }
    };

    static int size_jumptable( int segcount ) {
        /* 4-align, extra item for "end of memory" */
        return 2 * (2 + segcount - segcount % 2);
    }

    static int size_bitmap( int bytecount ) {
        /* NB. 4 * (x / 32) is NOT the same as x / 8 */
        return 4 * (bytecount / 32) + ((bytecount % 32) ? 4 : 0);
    }

    static int size_heap( int segcount, int bytecount ) {
        return sizeof( Heap ) +
            bytecount +
            size_jumptable( segcount ) +
            size_bitmap( bytecount );
    }

    struct Heap {
        int segcount;
        char _memory[0];

        int size() {
            return jumptable( segcount ) * 4;
        }

        uint16_t &jumptable( int segment ) {
            return reinterpret_cast< uint16_t * >( _memory )[ segment ];
        }

        uint16_t &jumptable( Pointer p ) {
            assert( owns( p ) );
            return jumptable( p.segment );
        }

        uint16_t &bitmap( Pointer p ) {
            assert( owns( p ) );
            return reinterpret_cast< uint16_t * >(
                _memory + size_jumptable( segcount ) )[ p.segment / 64 ];
        }

        int offset( Pointer p ) {
            assert( owns( p ) );
            return int( jumptable( p ) * 4 ) + p.offset;
        }

        int size( Pointer p ) {
            assert( owns( p ) );
            return 4 * (jumptable( p.segment + 1 ) - jumptable( p ));
        }

        uint16_t mask( Pointer p ) {
            assert_eq( offset( p ) % 4, 0 );
            return 1 << ((offset( p ) % 64) / 4);
        }

        StateAddress advance( StateAddress a, int ) {
            return StateAddress( a, 0, size_heap( segcount, size() ) );
        }
        int end() { return 0; }

        void setPointer( Pointer p, bool ptr ) {
            if ( ptr )
                bitmap( p ) |= mask( p );
            else
                bitmap( p ) &= ~mask( p );
        }

        bool isPointer( Pointer p ) {
            return bitmap( p ) & mask( p );
        }

        bool owns( Pointer p ) {
            return p.heap && p.segment < segcount;
        }

        char *dereference( Pointer p ) {
            assert( owns( p ) );
            return _memory + size_bitmap( size() ) + size_jumptable( segcount ) + offset( p );
        }
    };

    struct Nursery {
        std::vector< int > offsets;
        std::vector< char > memory;
        std::vector< bool > pointer;
        int segshift;

        Pointer malloc( int size ) {
            int segment = offsets.size() - 1;
            int start = offsets[ segment ];
            int end = start + size;
            align( end, 4 );
            offsets.push_back( end );
            memory.resize( end );
            pointer.resize( end / 4 );
            std::fill( memory.begin() + start, memory.end(), 0 );
            std::fill( pointer.begin() + start / 4, pointer.end(), 0 );
            return Pointer( segment + segshift, 0 );
        }

        bool owns( Pointer p ) {
            return p.heap && (p.segment - segshift < offsets.size() - 1);
        }

        int offset( Pointer p ) {
            assert( owns( p ) );
            return offsets[ p.segment - segshift ] + p.offset;
        }

        int size( Pointer p ) {
            assert( owns( p ) );
            return offsets[ p.segment - segshift + 1] - offsets[ p.segment - segshift ];
        }

        bool isPointer( Pointer p ) {
            assert_eq( p.offset % 4, 0 );
            return pointer[ offset( p ) / 4 ];
        }

        void setPointer( Pointer p, bool is ) {
            pointer[ offset( p ) / 4 ] = is;
        }

        char *dereference( Pointer p ) {
            assert( owns( p ) );
            return &memory[ offset( p ) ];
        }

        void reset( int shift ) {
            segshift = shift;
            memory.clear();
            offsets.clear();
            offsets.push_back( 0 );
            pointer.clear();
        }
    };

    Blob _blob, _stack;
    Nursery nursery;
    ProgramInfo &_info;
    Allocator &_alloc;
    int _thread; /* the currently scheduled thread */
    int _thread_count;
    Frame *_frame; /* pointer to the currently active frame */
    bool _blob_private;

    template< typename T >
    using Lens = lens::Lens< StateAddress, T >;

    struct Flags {
        uint64_t assert:1;
        uint64_t null_dereference:1;
        uint64_t invalid_dereference:1;
        uint64_t invalid_argument:1;
        uint64_t ap:48;
        uint64_t buchi:12;
    };

    typedef lens::Array< Frame > Stack;
    typedef lens::Array< Stack > Threads;
    typedef lens::Tuple< Flags, Globals, Heap, Threads > State;

    bool globalPointer( Pointer p ) {
        return !p.heap && p.segment == 1;
    }

    bool validate( Pointer p ) {
        return !p.null() && ( globalPointer( p ) || heap().owns( p ) || nursery.owns( p ) );
    }

    Lens< State > state() {
        return Lens< State >( StateAddress( &_info, _blob, _alloc._slack ) );
    }

    char *globalmem() {
        return state().get( Globals() ).memory;
    }

    char *dereference( Pointer p ) {
        assert( validate( p ) );
        if ( globalPointer( p ) )
            return globalmem() + p.offset;
        else if ( heap().owns( p ) )
            return heap().dereference( p );
        else
            return nursery.dereference( p );
    }

    /*
     * Get a pointer to the value storage corresponding to "v". If "v" is a
     * local variable, look into thread "thread" (default, i.e. -1, for the
     * currently executing one) and in frame "frame" (default, i.e. 0 is the
     * topmost frame, 1 is the frame just below that, etc...).
     */
    char *dereference( ProgramInfo::Value v, int tid = -1, int frame = 0 )
    {
        char *block = _frame->memory;

        if ( tid < 0 )
            tid = _thread;

        if ( !v.global && !v.constant && ( frame || tid != _thread ) )
            block = stack( tid ).get( stack( tid ).get().length() - frame - 1 ).memory;

        if ( v.global )
            block = globalmem();

        if ( v.constant )
            block = &_info.constdata[0];

        return block + v.offset;
    }

    Lens< Threads > threads() {
        return state().sub( Threads() );
    }

    Lens< Stack > _blob_stack( int i ) {
        assert_leq( 0, i );
        return state().sub( Threads(), i );
    }

    Lens< Stack > stack( int thread = -1 ) {
        if ( thread == _thread || thread < 0 ) {
            assert_leq( 0, _thread );
            return Lens< Stack >( StateAddress( &_info, _stack, 0 ) );
        } else
            return _blob_stack( thread );
    }

    Heap &heap() {
        return state().get( Heap() );
    }

    Frame &frame( int thread = -1, int idx = 0 ) {
        if ( ( thread == _thread || thread < 0 ) && !idx )
             return *_frame;

        auto s = stack( thread );
        return s.get( s.get().length() - idx - 1 );
    }

    Flags &flags() {
        return state().get( Flags() );
    }

    void enter( int function ) {
        int framesize = _info.functions[ function ].framesize;
        int depth = stack().get().length();
        bool masked = depth ? frame().pc.masked : false;
        stack().get().length() ++;

        _frame = &stack().get( depth );
        _frame->pc = PC( function, 0, 0 );
        _frame->pc.masked = masked; /* inherit */
        std::fill( _frame->memory, _frame->memory + framesize, 0 );
    }

    void leave() {
        int fun = frame().pc.function;
        auto &s = stack().get();
        s.length() --;
        if ( stack().get().length() )
            _frame = &stack().get( stack().get().length() - 1 );
        else
            _frame = nullptr;
    }

    template< typename F >
    void eachframe( Lens< Stack > s, F f ) {
        int idx = 0;
        auto address = s.sub( 0 ).address();
        while ( idx < s.get().length() ) {
            Frame &fr = address.as< Frame >();
            f( fr );
            address = fr.advance( address, 0 );
            ++ idx;
        }
    }

    void resnap();
    void switch_thread( int thread );
    int new_thread();

    int pointerSize( Pointer p );
    Pointer followPointer( Pointer p );

    void trace( Pointer p, Canonic &canonic );
    void trace( Frame &f, Canonic &canonic );
    void snapshot( Pointer from, Pointer to, Canonic &canonic, Heap &heap );
    void snapshot( Frame &f, Canonic &canonic, Heap &heap, StateAddress &address );
    Blob snapshot();
    void rewind( Blob, int thread = 0 );

    int size( int stack, int heapbytes, int heapsegs ) {
        return sizeof( Flags ) +
               sizeof( int ) + /* thread count */
               stack + size_heap( heapsegs, heapbytes ) + _info.globalsize;
    }

    MachineState( ProgramInfo &i, Allocator &alloc )
        : _stack( 4096 ), _info( i ), _alloc( alloc ), _blob_private( false )
    {
        _thread_count = 0;
        _frame = nullptr;
        nursery.reset( 0 ); /* nothing in the heap */
    }

    void dump( std::ostream & );
    void dump();
};

}
}

#endif
