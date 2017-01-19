// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2017 Petr Ročkai <code@fixp.eu>
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

#include <tools/benchmark.hpp>

#include <divine/ui/sysinfo.hpp>
#include <divine/ui/log.hpp>
#include <divine/ui/odbc.hpp>
#include <divine/ui/cli.hpp>

#include <iostream>
#include <vector>
#include <brick-string>
#include <brick-types>
#include <brick-cmd>

namespace benchmark
{

int Import::modrev()
{
    nanodbc::statement rev(
            _conn, "select max(revision) from model where name = ? group by name" );
    rev.bind( 0, _name.c_str() );

    int next = 1;
    try
    {
        auto r = nanodbc::execute( rev );
        r.first();
        next = 1 + r.get< int >( 0 );
    } catch (...) {}

    return next;
}

void Import::files()
{
    if ( _name.empty() )
        _name = _files[0];

    int script_id = odbc::unique_id( _conn, "source", odbc::Keys{ "text" },
                                     odbc::Vals{ fs::readFile( _script ) } );

    odbc::Keys keys_mod{ "name", "revision", "script" };
    odbc::Vals vals_mod{ _name, modrev(), script_id };
    _id = odbc::unique_id( _conn, "model", keys_mod, vals_mod );

    for ( auto file : _files )
    {
        auto src = fs::readFile( file );
        int file_id = odbc::unique_id(
                _conn, "source", odbc::Keys{ "text" },
                                 odbc::Vals{ src } );

        odbc::Keys keys_tie{ "model", "source", "filename" };
        odbc::Vals vals_tie{ _id, file_id, file };
        auto ins = odbc::insert( _conn, "model_srcs", keys_tie, vals_tie );
        nanodbc::execute( ins );
    };
}

void Import::tag()
{
    for ( auto tag : _tags )
    {
        int tag_id = odbc::unique_id( _conn, "tag", odbc::Keys{ "name" }, odbc::Vals{ tag } );
        odbc::Vals vals{ _id, tag_id };
        auto ins = odbc::insert( _conn, "model_tags", odbc::Keys{ "model", "tag" }, vals );
        ins.execute();
    }
}

void Schedule::run()
{
    int inst = odbc::get_instance( _conn );
    /* XXX check that highest id is always the latest revision? */
    auto mod = nanodbc::execute( _conn, "select max( id ), name from model group by name" );
    while ( mod.next() )
    {
        nanodbc::statement ins( _conn, "insert into job ( model, instance, status ) "
                                       "values (?, ?, 'P')" );
        int mod_id = mod.get< int >( 0 );
        ins.bind( 0, &mod_id );
        ins.bind( 1, &inst );
        ins.execute();
        std::cerr << "scheduled " << mod.get< std::string >( 1 ) << std::endl;
    }
}

void Cmd::setup()
{
    try
    {
        _conn.connect( _odbc );
        std::cerr << "connected to " << _odbc << std::endl;
    }
    catch ( nanodbc::database_error &err )
    {
        std::cerr << "could not connect: " << err.what() << std::endl;
    }
}

}

namespace cmd  = brick::cmd;
using namespace benchmark;

int main( int argc, const char **argv )
{
    auto validator = cmd::make_validator();
    auto args = cmd::from_argv( argc, argv );

    auto opts_db = cmd::make_option_set< Cmd >( validator )
        .option( "-d {string}", &Cmd::_odbc, "ODBC connection string" );

    auto opts_import = cmd::make_option_set< Import >( validator )
        .option( "[--name {string}]", &Import::_name, "model name (default: filename)" )
        .option( "[--tag {string}]", &Import::_tags, "tag(s) to assign to the model" )
        .option( "[--file {string}]", &Import::_files, "a (source) file to import" )
        .option( "[--script {string}]", &Import::_script, "a build/verify script" );

    auto opts_report = cmd::make_option_set< Report >( validator )
        .option( "[--watch]",  &Report::_watch, "refresh the results in a loop" )
        .option( "[--by-tag]",  &Report::_by_tag, "group results by tags" )
        .option( "[--list-instances]",  &Report::_list_instances, "show available instances" )
        .option( "[--result {string}]", &Report::_result, "only include runs with one of given results (default: VE)" )
        .option( "[--instance {int}]",  &Report::_instance, "show results for a given instance" );

    auto cmds = cmd::make_parser( cmd::make_validator() )
        .command< Import >( opts_db, opts_import )
        .command< Schedule >( opts_db )
        .command< Report >( opts_db, opts_report )
        .command< Run >( opts_db );
    auto cmd = cmds.parse( args.begin(), args.end() );

    cmd.match( [&]( Cmd &cmd ) { cmd.setup(); cmd.run(); } );
    return 0;
}
