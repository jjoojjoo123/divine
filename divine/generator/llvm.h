// -*- C++ -*- (c) 2007, 2008, 2009 Petr Rockai <me@mornfall.net>

#ifdef O_LLVM
#include <stdint.h>
#include <iostream>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/ADT/OwningPtr.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Constants.h>
#include <llvm/Support/system_error.h>

#include <divine/generator/common.h>
#include <divine/llvm/interpreter.h>
#include <divine/ltl2ba/main.h>

#ifndef DIVINE_GENERATOR_LLVM_H
#define DIVINE_GENERATOR_LLVM_H

namespace divine {
namespace generator {

using namespace ::llvm;

struct LLVM : Common< Blob > {
    typedef Blob Node;
    divine::llvm::Interpreter *_interpreter, *_interpreter_2;
    Module *_module;
    OwningPtr< MemoryBuffer > *_input;
    LLVMContext *ctx;
    std::string file;
    Node _initial;
    typedef wibble::Unit Label;

    typedef std::vector< int > PropGuard;
    typedef std::pair< PropGuard, int > PropTrans;

    std::vector< std::vector< int > > prop_next;
    std::vector< PropTrans > prop_trans;
    std::vector< bool > prop_accept;
    int prop_initial;
    ReductionSet reduce;

    bool use_property;

    template< typename Yield >
    void initials( Yield yield )
    {
        assert( _module );
        Function *f = _module->getFunction( "main" );
        assert( f );
        yield( Node(), interpreter().initial( f ), Label() );
    }

    bool buchi_enabled( PropGuard guard, unsigned ap ) {
        for ( auto g : guard ) {
            if ( g < 0 && (ap & (1 << (-g - 1))) ) return false;
            if ( g > 0 && !(ap & (1 << ( g - 1))) ) return false;
        }
        return true;
    }

    template< typename Yield >
    void successors( Node st, Yield yield ) {
        interpreter().run( st, [&]( Node n ){
                std::vector< int > buchi_succs;
                for ( auto next : prop_next[ flags( n ).buchi ] ) {
                    auto trans = prop_trans[ next ];
                    if ( buchi_enabled( trans.first, flags( n ).ap ) )
                        buchi_succs.push_back( trans.second );
                }

                if ( buchi_succs.empty() ) {
                    this->release( n );
                    return;
                }

                while ( buchi_succs.size() > 1 ) {
                    Blob b( this->pool(), n.size() );
                    n.copyTo( b );
                    flags( b ).buchi = buchi_succs.back();
                    buchi_succs.pop_back();
                    yield( b, Label() );
                }

                flags( n ).buchi = buchi_succs.front();
                yield( n, Label() );
            } );
    }

    void release( Node s ) {
        s.free( pool() );
    }

    divine::llvm::MachineState::Flags &flags( Blob b ) {
        return b.get< divine::llvm::MachineState::Flags >( alloc._slack );
    }

    bool isGoal( Node n ) {
        auto fl = flags( n );
        return fl.bad();
    }

    bool isAccepting( Node n ) {
        if ( !use_property )
            return false;
        return prop_accept[ flags( n ).buchi ];
    }

    std::string showNode( Node n ) {
        interpreter(); /* ensure _interpreter_2 is initialised */
        _interpreter_2->rewind( n );
        std::string s = _interpreter_2->describe();
        if ( use_property ) {
            int buchi = _interpreter_2->state.flags().buchi;
            s += "\nLTL: " + wibble::str::fmt( buchi ) + " (";
            for ( int i = 0; i < int( prop_next[ buchi ].size() ); ++i ) {
                int next = prop_next[buchi][i];
                s += wibble::str::fmt( prop_trans[next].first ) + " -> " +
                     wibble::str::fmt( prop_trans[next].second ) + "; ";
            }
            s += ")";
        }
        return s;
    }

    std::string showTransition( Node from, Node to, Label ) {
        return ""; // dummy
    }

    void die( std::string err ) __attribute__((noreturn)) {
        std::cerr << err << std::endl;
        exit( 1 );
    }

    void read( std::string _file ) {
        file = _file;
    }

    template< typename Y >
    void properties( Y yield ) {
        yield( "deadlock", "(deadlock reachability)", PT_Deadlock );
        yield( "assert", "(assertion violations)", PT_Goal );
        for ( auto p : interpreter().properties )
            yield( p.first, p.second, PT_Buchi );
    }

    int literal_id( std::string lit ) {
        MDNode *ap = interpreter().findEnum( "AP" );
        assert( ap );
        for ( int i = 0; i < int( ap->getNumOperands() ); ++i ) {
            MDNode *it = cast< MDNode >( ap->getOperand(i) );
            MDString *name = cast< MDString >( it->getOperand(1) );
            if ( name->getString() == lit )
                return 1 + cast< ConstantInt >( it->getOperand(2) )->getValue().getZExtValue();
        }
        assert_die();
    }

    void useProperty( std::string name ) {
        std::string ltl;

        if ( interpreter().properties.count( name ) )
            ltl = interpreter().properties[ name ];
        if ( ltl.empty() )
            return;

        use_property = true;
        BA_opt_graph_t b = buchi( ltl );

        typedef std::list< KS_BA_node_t * > NodeList;
        typedef std::list< BA_trans_t > TransList;
        std::set< std::string > lits;

        NodeList nodes, initial, accept;
        nodes = b.get_all_nodes();
        initial = b.get_init_nodes();
        accept = b.get_accept_nodes();

        assert_eq( initial.size(), 1U );
        prop_initial = initial.front()->name;
        prop_next.resize( nodes.size() );
        prop_accept.resize( nodes.size(), false );

        for ( NodeList::iterator n = nodes.begin(); n != nodes.end(); ++n ) {
            assert_leq( (*n)->name, int( nodes.size() ) );
            int nid = (*n)->name - 1;

            for ( TransList::const_iterator t = b.get_node_adj(*n).begin();
                  t != b.get_node_adj(*n).end(); ++t ) {
                std::vector< int > guard;

                for ( LTL_label_t::const_iterator l = t->t_label.begin();
                      l != t->t_label.end(); ++l ) {
                    assert_leq( 1, literal_id( l->predikat ) );
                    /* NB. Negation implies a *positive* query on an AP. */
                    guard.push_back( (l->negace ? 1 : -1) * literal_id( l->predikat ) );
                }
                prop_next[nid].push_back( prop_trans.size() );
                prop_trans.push_back( std::make_pair( guard, t->target->name - 1 ) );
            }
        }
        for ( NodeList::iterator n = accept.begin(); n != accept.end(); ++n )
            prop_accept[(*n)->name - 1] = true;
    }

    void applyReductions() {
        if ( reduce.count( R_Tau ) )
            interpreter().tauminus = true;
        if ( reduce.count( R_TauPlus ) ) {
            interpreter().tauminus = true;
            interpreter().tauplus = true;
        }
        if ( reduce.count( R_TauStores ) ) {
            interpreter().tauminus = true;
            interpreter().taustores = true;
        }
    }

    ReductionSet useReductions( ReductionSet r ) {
        if ( r.count( R_Tau ) )
            reduce.insert( R_Tau );
        if ( r.count( R_TauPlus ) )
            reduce.insert( R_TauPlus );
        if ( r.count( R_TauStores ) )
            reduce.insert( R_TauStores );

        reduce.insert( R_Heap );

        if ( _interpreter )
            applyReductions();

        return reduce;
    }

    divine::llvm::Interpreter &interpreter() {
        if (_interpreter)
            return *_interpreter;

        _input = new OwningPtr< MemoryBuffer >();
        ctx = new LLVMContext();
        MemoryBuffer::getFile( file, *_input );
        Module *m = ParseBitcodeFile( &**_input, *ctx );

        assert( m );
        std::string err;

        _interpreter = new divine::llvm::Interpreter( alloc, m );
        _interpreter_2 = new divine::llvm::Interpreter( alloc, m );
        _module = m;
        applyReductions();

        return *_interpreter;
    }

    LLVM() : _interpreter( 0 ), use_property( false ) {}
    LLVM( const LLVM &other ) {
        *this = other;
        _interpreter = 0;
        _initial = Node();
    }

};

}
}

#endif
#endif
