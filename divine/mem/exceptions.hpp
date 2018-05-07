// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2018 Adam Matoušek <xmatous3@fi.muni.cz>
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

#include <brick-types>
#include <divine/vm/divm.h>

namespace divine::mem
{

template< typename ExceptionType, typename Loc_ >
struct ExceptionMap
{
    using Loc = Loc_;
    using Internal = typename Loc::Internal;
    using ExcMap = std::map< Loc, ExceptionType >;
    using Lock = std::lock_guard< std::mutex >;

    ExceptionMap &operator=( const ExceptionType & o ) = delete;

    ExceptionType &at( Internal obj, int wpos )
    {
        Lock lk( _mtx );

        auto it = _exceptions.find( Loc( obj, wpos ) );
        ASSERT( it != _exceptions.end() );
        return it->second;
    }

    bool has( Internal obj, int wpos )
    {
        Lock lk( _mtx );

        auto it = _exceptions.find( Loc( obj, 0, wpos ) );
        return it != _exceptions.end();
    }

    void set( Internal obj, int wpos, const ExceptionType &exc )
    {
        Lock lk( _mtx );
        _exceptions[ Loc( obj, wpos ) ] = exc;
    }

    void free( Internal obj )
    {
        Lock lk( _mtx );

        auto lb = _exceptions.lower_bound( Loc( obj, 0 ) );
        auto ub = _exceptions.upper_bound( Loc( obj, (1 << _VM_PB_Off) - 1 ) );
        _exceptions.erase( lb, ub );
    }

    bool equal( Internal a, Internal b, int sz )
    {
        Lock lk( _mtx );

        auto lb_a = _exceptions.lower_bound( Loc( a, 0, 0 ) ),
             lb_b = _exceptions.lower_bound( Loc( b, 0, 0 ) );
        auto ub_a = _exceptions.upper_bound( Loc( a, 0, sz ) ),
             ub_b = _exceptions.upper_bound( Loc( b, 0, sz ) );

        auto i_b = lb_b;
        for ( auto i_a = lb_a; i_a != ub_a; ++i_a, ++i_b )
        {
            if ( i_b == ub_b ) return false;
            if ( i_a->first.offset != i_b->first.offset ) return false;
            if ( i_a->second != i_b->second ) return false;
        }

        if ( i_b != ub_b ) return false;

        return true;
    }

    template< typename OM >
    void copy( OM &from_m, typename OM::Loc from, Loc to, int sz )
    {
        Lock lk( _mtx );
        typename OM::Loc from_p( from.object, from.offset + sz );

        int delta = to.offset - from.offset;

        auto lb = from_m._exceptions.lower_bound( from );
        auto ub = from_m._exceptions.upper_bound( from_p );
        std::transform( lb, ub, std::inserter( _exceptions, _exceptions.begin() ), [&]( auto x )
        {
            auto fl = x.first;
            Loc l( to.object, fl.offset + delta );
            return std::make_pair( l, x.second );
        } );
    }

    bool empty()
    {
        Lock lk( _mtx );
        return std::all_of( _exceptions.begin(), _exceptions.end(),
                            []( const auto & e ) { return ! e.second.valid(); } );
    }

    ExcMap _exceptions;
    mutable std::mutex _mtx;
};

}
