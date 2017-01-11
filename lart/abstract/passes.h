// -*- C++ -*- (c) 2016 Henrich Lauko <xlauko@mail.muni.cz>
#pragma once

DIVINE_RELAX_WARNINGS
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Casting.h>
DIVINE_UNRELAX_WARNINGS

#include <divine/cc/clang.hpp>
#include <divine/cc/compile.hpp>
#include <divine/cc/options.hpp>
#include <divine/rt/runtime.hpp>

#include <lart/support/pass.h>
#include <lart/support/meta.h>
#include <lart/support/util.h>
#include <lart/support/query.h>

#include <lart/abstract/passes.h>
#include <lart/abstract/types.h>
#include <lart/abstract/intrinsic.h>

#include <brick-string>
#include <brick-llvm>

#include <fstream>
#include <string>

#include <lart/abstract/abstraction.h>
#include <lart/abstract/assume.h>
#include <lart/abstract/substitution.h>

namespace lart {
namespace abstract {

    PassMeta abstraction_pass();
    PassMeta assume_pass();
    PassMeta substitution_pass();

    PassMeta full_abstraction_pass() {
    return passMetaC< Abstraction, Substitution >( "abstraction", "",
        []( llvm::ModulePassManager &mgr, std::string opt ) {
            Abstraction::meta().create( mgr, "" );
            AddAssumes::meta().create( mgr, "" );
            Substitution::meta().create( mgr, opt );
        } );
    };

    inline std::vector< PassMeta > passes() {
        return { full_abstraction_pass() };
    }
}

#ifdef BRICK_UNITTEST_REG
namespace t_abstract {

using Compile = divine::cc::Compile;
using ModulePtr = Compile::ModulePtr;
void mapVirtualFile( Compile & c, const std::string & path, const std::string & src ) {
    c.setupFS( [&]( std::function< void( std::string, const std::string & ) > yield ) {
        yield( path, src );
    } );
}

std::string source( std::string fileName ) {
    std::string res;
    divine::rt::each( [&]( auto path, auto src ) {
        if ( brick::string::endsWith( path, fileName ) )
            res = src;
    } );
    return res;
}

void setupFS( Compile & c ) {
	auto each = [&]( auto filter ) {
        return [&filter]( std::function< void( std::string, const std::string & ) > yield ) {
            divine::rt::each( filter( yield ) );
        };
    };

    c.setupFS( each( [&]( std::function< void( std::string, const std::string & ) > yield ) {
        return [&]( auto path, auto src ) {
			if ( !brick::string::endsWith( path, ".bc" ) )
				yield( path, src );
		};
	} ) );
}

ModulePtr compile( const std::string & src,
                   const std::vector< std::string > & link = {},
                   const std::vector< std::string > & headers = {} )
{
    static std::shared_ptr< llvm::LLVMContext > ctx( new llvm::LLVMContext );
    const bool dont_link = false;
    const bool verbose = false;
    Compile c( { dont_link, verbose }, ctx );

    mapVirtualFile( c, "main.cpp", src );
    for ( const auto & f : link )
        mapVirtualFile( c, f, source( f ) );
    for ( const auto & f : headers )
        mapVirtualFile( c, f, source( f ) );

    setupFS( c );
    std::vector< std::string > flags = { "-std=c++11" };

    brick::llvm::Linker linker;
    linker.load( c.compile( "main.cpp", flags ) );
    for ( const auto & f : link )
        linker.link( c.compile( f, flags ) );
    return linker.take();
}

std::string load( const std::string & path ) {
    std::ifstream file( path );
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ModulePtr test_abstraction( const std::string & src ) {
    auto m = compile( src );

    llvm::ModulePassManager manager;

    abstract::abstraction_pass().create( manager, "" );

    manager.run( *m );
    return m;
}

ModulePtr test_assume( const std::string & src ) {
    auto m = compile( src );

    llvm::ModulePassManager manager;

    abstract::abstraction_pass().create( manager, "" );
    abstract::assume_pass().create( manager, "" );

    manager.run( *m );
    return m;
}

ModulePtr test_substitution( const std::string & src,
                             const std::string & opt )
{
    std::vector< std::string > link = { opt + ".cpp", "tristate.cpp" };
    std::vector< std::string > headers = { "tristate.h", "common.h", opt + ".h" };
    auto m = compile( src, link, headers );
    llvm::ModulePassManager manager;

    abstract::abstraction_pass().create( manager, "" );
    abstract::substitution_pass().create( manager, opt );

    manager.run( *m );

    return m;
}

ModulePtr test_zero_substitution( const std::string & src ) {
    return test_substitution( src, "zero" );
}

namespace {
const std::string annotation =
                  "#define __test __attribute__((__annotate__(\"lart.abstract.test\")))\n";

bool containsUndefValue( llvm::Function &f ) {
    for ( auto & bb : f ) {
        for ( auto & i : bb ) {
            if ( llvm::isa< llvm::UndefValue >( i ) )
                return true;
            for ( auto & op : i.operands() )
                if ( llvm::isa< llvm::UndefValue >( op ) )
                    return true;
        }
    }

    return false;
}

bool containsUndefValue( llvm::Module &m ) {
    for ( auto & f : m ) {
        if ( containsUndefValue( f ) )
            return true;
    }

    return false;
}

bool liftingPointer( llvm::Module &m ) {
    return query::query( m ).flatten().flatten()
           .map( query::refToPtr )
           .map( query::llvmdyncast< llvm::CallInst > )
           .filter( query::notnull )
           .filter( [&]( llvm::CallInst * call ) {
                return abstract::intrinsic::isLift( call );
            } )
            .any( []( llvm::CallInst * call ) {
                return call->getOperand( 0 )->getType()
                           ->isPointerTy();
            } );
}

} //empty namespace

struct Abstraction {
    TEST( simple ) {
        auto s = "int main() { return 0; }";
        test_abstraction( annotation + s );
    }

    TEST( create ) {
        auto s = R"(int main() {
                        __test int abs;
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto f = m->getFunction( "lart.abstract.alloca.i32" );
        ASSERT( f->hasOneUse() );
    }

    TEST( types ) {
        auto s = R"(int main() {
                        __test short abs_s;
                        __test int abs;
                        __test long abs_l;
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto f16 = m->getFunction( "lart.abstract.alloca.i16" );
        ASSERT( f16->hasOneUse() );
        auto f32 = m->getFunction( "lart.abstract.alloca.i32" );
        ASSERT( f32->hasOneUse() );
        auto f64 = m->getFunction( "lart.abstract.alloca.i64" );
        ASSERT( f64->hasOneUse() );
    }

    TEST( binary_ops ) {
        auto s = R"(int main() {
                        __test int abs;
                        int a = abs + 42;
                        int b = abs + a;
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( phi ) {
        auto s = R"(int main() {
                        __test int x = 0;
                        __test int y = 42;
                        int z = x || y;
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto nodes = query::query( *m ).flatten().flatten()
                           .map( query::refToPtr )
                           .map( query::llvmdyncast< llvm::PHINode > )
                           .filter( query::notnull )
                           .freeze();
        ASSERT_EQ( nodes.size(), 1 );
        ASSERT_EQ( nodes[0]->getNumIncomingValues(), 2 );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( call_simple ) {
        auto s = R"(int call( int arg ) { return arg; }
                    int main() {
                        __test int abs;
                        int ret = call( abs );
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto main = m->getFunction( "main" );
        auto alloca = m->getFunction( "lart.abstract.alloca.i32" );
        auto abstract_call = m->getFunction( "_Z4calli.2.3" );

        auto i32_t = llvm::Type::getInt32Ty( m->getContext() );
        ASSERT_EQ( abstract_call->getReturnType()
                 , alloca->getReturnType()->getPointerElementType() );
        ASSERT_EQ( abstract_call->getFunctionType()->getParamType( 0 )
                 , alloca->getReturnType()->getPointerElementType() );

        ASSERT_EQ( main->getReturnType(), i32_t );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( call_propagate ) {
        auto s = R"(int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto abstract_call = m->getFunction( "_Z4callv.2" );
        auto alloca = m->getFunction( "lart.abstract.alloca.i32" );

        ASSERT_EQ( abstract_call->getReturnType()
                 , alloca->getReturnType()->getPointerElementType() );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( call_propagate_deeper ) {
        auto s = R"(int call2( int x ) {
                        return x * x;
                    }
                    int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        call2( ret );
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( call_propagate_deeper_2 ) {
        auto s = R"(
                    int call4( int x ) { return x; }
                    int call3( int x ) { return x; }
                    int call2( int x ) {
                        return call3( x ) * call4( x );
                    }
                    int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        call2( ret );
                        return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( tristate ) {
        auto s = R"(int main() {
                        __test int x;
                        if ( x > 0 )
                            return 42;
                        else
                            return 0;
                    })";
        auto m = test_abstraction( annotation + s );
        auto sgt = m->getFunction( "lart.abstract.icmp_sgt.i32" );
        ASSERT( abstract::types::IntegerType::isa( sgt->getReturnType(), 1 ) );
        auto to_tristate = m->getFunction( "lart.abstract.bool_to_tristate" );
        ASSERT_EQ( to_tristate->user_begin()->getOperand( 0 ), *sgt->user_begin() );
        auto lower = m->getFunction( "lart.tristate.lower" );
        ASSERT_EQ( lower->user_begin()->getOperand( 0 ), *to_tristate->user_begin() );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( lift ) {
        auto s = R"(int main() {
                        __test int x;
                        return x + 42;
                    })";
        auto m = test_abstraction( annotation + s );
        auto lift = m->getFunction( "lart.abstract.lift.i32" )->user_begin();
        auto val = llvm::cast< llvm::ConstantInt >( lift->getOperand( 0 ) );
        ASSERT( val->equalsInt( 42 ) );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( lift_replace ) {
        auto s = R"(int main() {
                        __test int x;
                        __test int y;
                        return x + y;
                    })";
        auto m = test_abstraction( annotation + s );
        auto lift = m->getFunction( "lart.abstract.lift.i32" );
        ASSERT( lift == nullptr );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( switch_test ) {
        auto s = R"(int main() {
                        __test int x;
                        int i = 0;
                        switch( x ) {
                            case 0: i = x; break;
                            case 1: i = (-x); break;
                            case 2: i = 42; break;
                        }
                    })";
        auto m = test_abstraction( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }

    TEST( loop_test ) {
        auto s = R"(int main() {
                        __test int x;
                        for ( int i = 0; i < x; ++i )
                            for ( int j = 0; j < x; ++j )
                                for ( int k = 0; k < x; ++k ) {
                                    __test int y = i * j *k;
                                }
                    })";
        auto m = test_abstraction( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
        ASSERT( ! liftingPointer( *m ) );
    }
};

struct Assume {
    bool isTristateAssume( llvm::Instruction * inst ) {
        if ( auto call = llvm::dyn_cast< llvm::CallInst >( inst ) ) {
            auto fn = call->getCalledFunction();
            return fn->hasName() && fn->getName().startswith( "lart.tristate.assume" );
        }
        return false;
    }

    bool isTrueAssume( llvm::Instruction * inst ) {
        return isTristateAssume( inst )
            && ( inst->getOperand( 1 ) == abstract::types::Tristate::True() );
    }

    bool isFalseAssume( llvm::Instruction * inst ) {
        return isTristateAssume( inst )
            && ( inst->getOperand( 1 ) == abstract::types::Tristate::False() );
    }

    void testBranching( llvm::Instruction * lower ) {
        auto br = llvm::cast< llvm::BranchInst >( *lower->user_begin() );

        auto trueBB = br->getSuccessor( 0 );
        ASSERT( isTrueAssume( trueBB->begin() ) );
        auto falseBB = br->getSuccessor( 1 );
        ASSERT( isFalseAssume( falseBB->begin() ) );
    }

    TEST( simple ) {
        auto s = "int main() { return 0; }";
        test_assume( annotation + s );
    }

    TEST( tristate ) {
        auto s = R"(int main() {
                        __test int x;
                        if ( x > 0 )
                            return 42;
                        else
                            return 0;
                    })";
        auto m = test_assume( annotation + s );
        auto icmp = m->getFunction( "lart.abstract.icmp_sgt.i32" );
        ASSERT( abstract::types::IntegerType::isa( icmp->getReturnType(), 1 ) );
        auto to_tristate = m->getFunction( "lart.abstract.bool_to_tristate" );
        ASSERT_EQ( to_tristate->user_begin()->getOperand( 0 ), *icmp->user_begin() );
        auto lower = llvm::cast< llvm::Instruction >(
                     *m->getFunction( "lart.tristate.lower" )->user_begin() );
        ASSERT_EQ( lower->getOperand( 0 ), *to_tristate->user_begin() );

        testBranching( lower );
    }

    TEST( loop ) {
        auto s = R"(int main() {
                        __test int abs;
                        while( abs ) {
                            ++abs;
                        }
                        return 0;
                    })";
        auto m = test_assume( annotation + s );

        auto icmp = m->getFunction( "lart.abstract.icmp_ne.i32" );
        ASSERT( abstract::types::IntegerType::isa( icmp->getReturnType(), 1 ) );
        auto to_tristate = m->getFunction( "lart.abstract.bool_to_tristate" );
        ASSERT_EQ( to_tristate->user_begin()->getOperand( 0 ), *icmp->user_begin() );
        auto lower = llvm::cast< llvm::Instruction >(
                     *m->getFunction( "lart.tristate.lower" )->user_begin() );
        ASSERT_EQ( lower->getOperand( 0 ), *to_tristate->user_begin() );

        testBranching( lower );
    }
};

struct Substitution {
    TEST( simple ) {
        auto s = "int main() { return 0; }";
        test_zero_substitution( annotation + s );
    }

    TEST( create ) {
        auto s = R"(int main() {
                        __test int abs;
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto f = m->getFunction( "__abstract_zero_alloca" );
        ASSERT( f->hasNUses( 2 ) );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( types ) {
        auto s = R"(int main() {
                        __test short abs_s;
                        __test int abs;
                        __test long abs_l;
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto alloca = m->getFunction( "__abstract_zero_alloca" );
        ASSERT( alloca->hasNUses( 4 ) );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( binary_ops ) {
        auto s = R"(int main() {
                        __test int abs;
                        int a = abs + 42;
                        int b = abs + a;
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto add = m->getFunction( "__abstract_zero_add" );
        ASSERT( add->hasNUses( 3 ) );

        auto lift = m->getFunction( "__abstract_zero_lift_i32" )->user_begin();
        ASSERT( llvm::isa< llvm::ConstantInt >( lift->getOperand( 0 ) ) );
        ASSERT_EQ( add->user_begin()->getOperand( 1 ), *lift );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( tristate ) {
        auto s = R"(int main() {
                        __test int x;
                        if ( x > 0 )
                            return 42;
                        else
                            return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );

        auto icmp = m->getFunction( "__abstract_zero_icmp_sgt" );
        ASSERT_EQ( icmp->getReturnType(),
                   m->getTypeByName( "struct.abstract::Zero" )->getPointerTo() );
        auto to_tristate = m->getFunction( "__abstract_zero_bool_to_tristate" );
        ASSERT_EQ( to_tristate->user_begin()->getOperand( 0 ), *icmp->user_begin() );
        auto lower = llvm::cast< llvm::Instruction >(
                     *m->getFunction( "__abstract_tristate_lower" )->user_begin() );
        ASSERT_EQ( lower->getOperand( 0 ), *to_tristate->user_begin() );

        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( phi ) {
        auto s = R"(int main() {
                        __test int x = 0;
                        __test int y = 42;
                        int z = x || y;
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto nodes = query::query( *m ).flatten().flatten()
                           .map( query::refToPtr )
                           .map( query::llvmdyncast< llvm::PHINode > )
                           .filter( query::notnull )
                           .freeze();

        ASSERT_EQ( nodes.size(), 1 );
        ASSERT_EQ( nodes[0]->getNumIncomingValues(), 2 );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( switch_test ) {
        auto s = R"(int main() {
                        __test int x;
                        int i = 0;
                        switch( x ) {
                            case 0: i = x; break;
                            case 1: i = (-x); break;
                            case 2: i = 42; break;
                        }
                    })";
        auto m = test_zero_substitution( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( lift ) {
        auto s = R"(int main() {
                        __test int x;
                        return x + 42;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto lift = m->getFunction( "__abstract_zero_lift_i32" )->user_begin();
        auto val = llvm::cast< llvm::ConstantInt >( lift->getOperand( 0 ) );
        ASSERT( val->equalsInt( 42 ) );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( loop_test ) {
        auto s = R"(int main() {
                        __test int x;
                        for ( int i = 0; i < x; ++i )
                            for ( int j = 0; j < x; ++j )
                                for ( int k = 0; k < x; ++k ) {
                                    __test int y = i * j *k;
                                }
                    })";
        auto m = test_zero_substitution( annotation + s );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( call_propagate_ones ) {
        auto s = R"(int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        auto abstract_call = m->getFunction( "_Z4callv.9.10" );
        ASSERT( abstract_call );
        auto alloca = m->getFunction( "__abstract_zero_alloca" );
        ASSERT_EQ( abstract_call->getReturnType()
                 , alloca->getReturnType()->getPointerElementType() );
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( call_propagate_deeper_1 ) {
        auto s = R"(int call2( int x ) {
                        return x * x;
                    }
                    int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        call2( ret );
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        //TODO asserts
        ASSERT( ! containsUndefValue( *m ) );
    }

    TEST( call_propagate_deeper_2 ) {
        auto s = R"(
                    int call4( int x ) { return x; }
                    int call3( int x ) { return x; }
                    int call2( int x ) {
                        return call3( x ) * call4( x );
                    }
                    int call() {
                        __test int x;
                        return x;
                    }
                    int main() {
                        int ret = call();
                        call2( ret );
                        return 0;
                    })";
        auto m = test_zero_substitution( annotation + s );
        //TODO asserts
        ASSERT( ! containsUndefValue( *m ) );
    }
};

} // namespace t_abstract
#endif
} // namespace lart
