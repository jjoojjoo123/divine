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

#include <divine/vm/run.hpp>
#include <divine/vm/bitcode.hpp>
#include <divine/vm/heap.hpp>
#include <divine/vm/eval.hpp>
#include <divine/vm/context.hpp>
#include <divine/vm/setup.hpp>

namespace divine {
namespace vm {

struct RunContext : Context< Program, MutableHeap >
{
    std::mt19937 _rand;
    using Context::Context;

    template< typename I >
    int choose( int o, I, I )
    {
        std::uniform_int_distribution< int > dist( 0, o - 1 );
        return dist( _rand );
    }

    void doublefault()
    {
        std::cerr << "E: Double fault, program terminated." << std::endl;
        this->set( _VM_CR_Frame, nullPointer() );
    }

    void trace( vm::TraceText tt ) { std::cerr << "T: " << heap().read_string( tt.text ) << std::endl; }
    void trace( vm::TraceSchedInfo ) { NOT_IMPLEMENTED(); }
    void trace( vm::TraceSchedChoice ) {}
};

void Run::run()
{
    using Eval = vm::Eval< Program, RunContext, value::Pointer >;
    auto &program = _bc->program();
    RunContext _ctx( program );
    Eval eval( program, _ctx );

    setup( _ctx );

    while ( !( _ctx.ref( _VM_CR_Flags ) & _VM_CF_Cancel ) )
    {
        schedule( eval );
        eval.run();
    }
}

}
}
