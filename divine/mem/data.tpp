// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2011-2018 Petr Ročkai <code@fixp.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <divine/mem/data.hpp>

namespace divine::mem
{

    template< typename Next >
    auto Data< Next >::snap_find( uint32_t obj ) const -> SnapItem *
    {
        auto begin = snap_begin(), end = snap_end();
        if ( !begin )
            return nullptr;

        while ( begin < end )
        {
            auto pivot = begin + (end - begin) / 2;
            if ( pivot->first > obj )
                end = pivot;
            else if ( pivot->first < obj )
                begin = pivot + 1;
            else
            {
                ASSERT( valid( pivot->second ) );
                return pivot;
            }
        }

        return begin;
    }

    template< typename Next >
    typename Data< Next >::Loc Data< Next >::make( int size, uint32_t hint, bool overwrite )
    {
        SnapItem *search = snap_find( hint );
        bool found = false;
        while ( !found )
        {
            found = true;
            if ( _l.exceptions.count( hint ) )
                found = false;
            if ( search && search != snap_end() && search->first == hint )
                ++ search, found = false;
            if ( overwrite && _l.exceptions.count( hint ) )
                found = !_l.exceptions[ hint ].slab();
            if ( !found )
                ++ hint;
        }
        ASSERT( !ptr2i( hint ).slab() );
        auto obj = _l.exceptions[ hint ] = objects().allocate( size );
        Next::materialise( obj, size );
        return Loc( obj, hint, 0 );
    }

    template< typename Next >
    bool Data< Next >::resize( Pointer p, int sz_new )
    {
        if ( p.offset() || !valid( p ) )
            return false;
        auto obj_old = ptr2i( p );
        int sz_old = size( obj_old );
        auto obj_new = objects().allocate( sz_new );

        Next::materialise( obj_new, sz_new );
        copy( *this, loc( p, obj_old ), *this, loc( p, obj_new ), std::min( sz_new, sz_old ), true );
        _l.exceptions[ p.object() ] = obj_new;
        return true;
    }

    template< typename Next >
    bool Data< Next >::free( Pointer p )
    {
        if ( !valid( p ) )
            return false;
        auto ex = _l.exceptions.find( p.object() );
        if ( ex == _l.exceptions.end() )
            _l.exceptions.emplace( p.object(), Internal() );
        else
        {
            Next::free( ex->second );
            objects().free( ex->second );
            ex->second = Internal();
        }
        if ( p.offset() )
            return false;
        return true;
    }

    template< typename Next > template< typename T >
    void Data< Next >::write( Loc l, T t )
    {
        using Raw = typename T::Raw;
        ASSERT_LEQ( sizeof( Raw ), size( l.object ) - l.offset );
        Next::write( l, t );
        *objects().template machinePointer< Raw >( l.object, l.offset ) = t.raw();
    }

    template< typename Next > template< typename T >
    void Data< Next >::read( Loc l, T &t ) const
    {
        using Raw = typename T::Raw;
        ASSERT_LEQ( sizeof( Raw ), size( l.object ) - l.offset );
        t.raw( *objects().template machinePointer< Raw >( l.object, l.offset ) );
        Next::read( l, t );
    }

    template< typename Next > template< typename FromH, typename ToH >
    bool Data< Next >::copy( FromH &from_h, typename FromH::Loc from, ToH &to_h, Loc to,
                             int bytes, bool internal )
    {
        int  from_s = from_h.size( from.object ),
             to_s   = to_h.size( to.object );
        auto from_b = from_h.unsafe_ptr2mem( from.object ),
             to_b   =   to_h.unsafe_ptr2mem( to.object );
        int  from_off = from.offset,
             to_off   = to.offset;

        if ( !from_b || !to_b || from_off + bytes > from_s || to_off + bytes > to_s )
            return false;

        Next::copy( from_h, from, to_h, to, bytes, internal );
        std::copy( from_b + from_off, from_b + from_off + bytes, to_b + to_off );

        return true;
    }

}

// vim: ft=cpp
