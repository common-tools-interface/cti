#pragma once

// std library
#include <string>
#include <map>

// dyninst symtab
#include <Symtab.h>

// dyninst processcontrol
#include <PCProcess.h>
#include <Event.h>

#define DEBUG(str, x) do { str << x; } while (0)
//#define DEBUG(str, x)

/* inferior: manages dyninst process info, symbols, breakpoints */

class MPIRInferior {
	using Symbol = Dyninst::SymtabAPI::Symbol;
	using Process = Dyninst::ProcControlAPI::Process;
	using Breakpoint = Dyninst::ProcControlAPI::Breakpoint;

	using SymbolMap = std::map<std::string, Symbol*>;

	/* RAII for symtab pointer */
	class SymtabHandle {
		using Symtab = Dyninst::SymtabAPI::Symtab;

		Symtab *symtab_ptr = NULL;
	public:
		SymtabHandle(std::string const& binary) {
			if (!Symtab::openFile(symtab_ptr, binary)) {
				throw std::runtime_error("Symtab failed to open file");
			}
		}

		~SymtabHandle() {
			Symtab::closeSymtab(symtab_ptr);
		}

		Symtab *get() const { return symtab_ptr; }
	};

	/* symbol table members */
	SymtabHandle symtab;
	SymbolMap symbols;

	/* ensure event is a breakpoint event, then invoke the function-specific handler */
	static Process::cb_ret_t on_breakpoint(Dyninst::ProcControlAPI::Event::const_ptr genericEv) {
		using Dyninst::ProcControlAPI::Breakpoint;

		if (auto ev = genericEv->getEventBreakpoint()) {

			/* get list of hit breakpoints */
			std::vector<Breakpoint::const_ptr> hitPoints;
			ev->getBreakpoints(hitPoints);

			/* should only ever have one breakpoint anyway */
			void *rawHandlerFn = hitPoints[0]->getData();
			auto handlerFn = *reinterpret_cast<HandlerFnType*>(rawHandlerFn);

			/* run the handler function */
			return handlerFn();
		}

		DEBUG(std::cerr, "invalid event type for on_breakpoint" << std::endl);
		return Process::cbProcStop;
	}
public:
	Process::ptr proc;

	/* breakpoint management */
	using HandlerFnType = Process::cb_ret_t();
	void setBreakpoint(std::string const& fnName, HandlerFnType* handlerPtr);
	static Process::cb_ret_t stop_on_breakpoint() { return Process::cbProcStop; }

	/* symbol management */
	void addSymbol(std::string const& symName);
	Symbol* getSymbol(std::string const& symName) {
		// if symbol address not found yet, find it
		if (symbols.find(symName) == symbols.end()) {
			addSymbol(symName);
		}

		return symbols.at(symName);
	}

	/* create a new process with arguments */
	MPIRInferior(std::string const& launcher, std::vector<std::string> const& launcherArgv,
		std::vector<std::string> envVars = {}, std::map<int, int> remapFds = {});
	/* attach to existing process */
	MPIRInferior(std::string const& launcher, Dyninst::PID pid);
	~MPIRInferior();
};