// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2016 Petr Ročkai <code@fixp.eu>
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

#include <divine/mc/ctx-assume.hpp>
#include <divine/mc/exec.hpp>
#include <divine/mc/search.hpp>
#include <divine/mc/bitcode.hpp>
#include <divine/mc/machine.hpp>
#include <divine/mc/weaver.hpp>
#include <divine/vm/eval.tpp>

namespace divine::mc
{
    template< typename next >
    struct print_trace : next
    {
        using next::trace;
        void trace( std::string s ) { std::cout << s << std::endl; }
        void trace( vm::TraceInfo ti ) { trace( this->heap().read_string( ti.text ) ); }
    };

    namespace ctx = vm::ctx;
    struct ctx_exec : brq::compose_stack< ctx_assume, ctx_choice, brq::module< print_trace >,
                                          ctx::with_debug, ctx::with_tracking,
                                          ctx::common< vm::Program, vm::CowHeap > > {};

    namespace event
    {
        struct infeasible : base {};
    }

    template< uint64_t flag, typename next >
    struct infeasible_notify_with_flag_ : next
    {
        bool feasible( typename next::tq q ) override
        {
            if ( next::feasible( q ) )
                return true;

            if ( this->context().flags_any( flag ) )
                this->push( q, event::infeasible() );

            return false;
        }
    };

    template< typename next >
    using infeasible_notify_ = infeasible_notify_with_flag_< _VM_CF_Cancel, next >;

    template< typename next >
    using on_exit_notify_ = infeasible_notify_with_flag_< _VM_CF_Stop, next >;

    struct backtrack : machine::tree_search
    {
        std::stack< task::choose > _stack;
        using machine::tree_search::run;

        void run( tq q, event::infeasible )
        {
            if ( !_stack.empty() )
            {
                TRACE( "encountered an infeasible path, backtracking at", _stack.top() );
                reply( q, _stack.top() );
                _stack.pop();
            }
        }

        void run( tq q, task::choose c )
        {
            if ( c.choice )
                _stack.push( c );
            else
                reply( q, c );
        }

        ~backtrack()
        {
            /* FIXME
            while ( !_stack.empty() )
                this->snap_put( _stack.top().snap ), _stack.pop();
            */
        }
    };

    struct coverage_heuristic
    {
        using choice_id = int;
        using task_pool_t = std::queue< task::choose >;

        std::unordered_map< choice_id, task_pool_t > _pools;
        std::unordered_map< choice_id, int > _counters;

        int top_id() noexcept
        {
            return std::min_element( _counters.begin(), _counters.end(),
                [] ( const auto& lhs, const auto& rhs ) {
                    return lhs.second < rhs.second;
                } )->first;
        }

        task::choose top() noexcept { return _pools[ top_id() ].front(); }

        void push( task::choose && c ) noexcept
        {
            auto id = c.weight + c.choice;

            if ( !_counters.count( id ) )
                _counters[ id ] = 0;
            _pools[ id ].push( std::move( c ) );
        }

        task::choose pop_task( task_pool_t & pool ) noexcept
        {
            auto top = pool.front();
            pool.pop();
            return top;
        }

        task::choose pop() noexcept
        {
            auto id = top_id();
            _counters[ id ]++;

            auto & pool = _pools[ id ];
            auto task = pop_task( pool );
            if ( pool.empty() )
                _pools.erase( id );
            return task;
        }

        bool empty() const noexcept { return _pools.empty(); }
    };

    template< typename heuristic_t >
    struct heuristic_search : machine::tree_search
    {
        heuristic_t _queue;

        using machine::tree_search::run;

        void run( tq q, event::infeasible )
        {
            if ( !_queue.empty() )
            {
                TRACE( "encountered an infeasible path, backtracking at", _queue.top() );
                reply( q, _queue.pop() );
            }
        }

        void run( tq q, task::choose c )
        {
            bool last_choice = c.choice == c.total - 1;
            _queue.push( std::move( c ) );

            if ( last_choice ) {
                TRACE( "heuristic choose continue at", _queue.top() );
                reply( q, _queue.pop() );
            }
        }
    };

    using infeasible_notify = brq::module< infeasible_notify_ >;
    using on_exit_notify = brq::module< on_exit_notify_ >;
    using queue_exec = task_queue< event::infeasible >;

    template< typename solver_t >
    struct mach_exec : brq::compose_stack< infeasible_notify, machine::compute,
                                           machine::with_context< ctx_exec >,
                                           machine::base< solver_t, queue_exec > > {};

    template< typename solver_t >
    void Exec::do_run()
    {
        backtrack b;
        mach_exec< solver_t > c;
        c.bc( _bc );
        c.context().enable_debug();
        weave( c, b ).start();
    }

    void Exec::run()
    {
        if ( _bc->is_symbolic() )
            do_run< smt::STPSolver >();
        else
            do_run< smt::NoSolver >();
    }
}

