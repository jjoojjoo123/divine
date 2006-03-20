#include <wibble/commandline/parser.h>
#include <wibble/commandline/doc.h>
#include <iostream>

using namespace std;

namespace wibble {
namespace commandline {

bool StandardParser::parse(int argc, const char* argv[])
{
	if (Parser::parse(argc, argv))
		return true;

	if (help->boolValue())
	{
		// Provide help as requested
		commandline::Help help(m_appname, m_version);
		commandline::Engine* e = m_engine.foundCommand();

		if (e)
			// Help on a specific command
			help.outputHelp(cout, *e);
		else
			// General help
			help.outputHelp(cout, m_engine);
		return true;
	}
	if (version->boolValue())
	{
		// Print the program version
		commandline::Help help(m_appname, m_version);
		help.outputVersion(cout);
		return true;
	}
	return false;
}

bool StandardParserWithManpage::parse(int argc, const char* argv[])
{
	if (StandardParser::parse(argc, argv))
		return true;
	if (manpage->boolValue())
	{
		// Output the manpage
		commandline::Manpage man(m_appname, m_version, m_section, m_author);
		string hooks(manpage->stringValue());
		if (!hooks.empty())
			man.readHooks(hooks);
		man.output(cout, m_engine);
		return true;
	}
	return false;
}

}
}


#ifdef COMPILE_TESTSUITE

#include <wibble/tests.h>

namespace tut {

struct wibble_commandline_parser_shar {
};
TESTGRP(wibble_commandline_parser);

using namespace wibble::commandline;

template<> template<>
void to::test<1>()
{
}

}

#endif

// vim:set ts=4 sw=4:
