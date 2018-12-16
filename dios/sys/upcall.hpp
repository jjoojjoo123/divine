// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2018 Petr Ročkai <code@fixp.eu>
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
#include <dios/sys/syscall.hpp>

namespace __dios
{

    template< typename Conf >
    struct Upcall : Conf
    {
        using Process = typename Conf::Process;
        using BaseProcess = typename BaseContext::Process;

        virtual void reschedule() override
        {
            if ( this->check_final() )
                this->finalize();
            /* destroy the stack to avoid memory leaks */
            __dios_unwind( nullptr, nullptr, nullptr );
            __vm_suspend();
        }

        virtual BaseProcess *make_process( BaseProcess *bp ) override
        {
            if ( bp )
                return new Process( *static_cast< Process * >( bp ) );
            else
                return new Process();
        }
    };

}
