#include <memory>

#include <fcntl.h>

#include "MPIRInstance.hpp"

#include "mpir_iface.h"

std::map<mpir_id_t, std::unique_ptr<MPIRInstance>> mpirInstances;

static const mpir_id_t INSTANCE_ERROR = -1;
mpir_id_t newId() {
	static mpir_id_t nextId = 1;
	return nextId++;
}

mpir_id_t _cti_mpir_newLaunchInstance(const char *launcher, const char * const launcher_args[],
	const char * const env_list[], int stdin_fd, int stdout_fd, int stderr_fd) {

	mpir_id_t id = newId();

	/* construct argv array & instance*/
	std::vector<std::string> launcherArgv{launcher};
	for (const char* const* arg = launcher_args; *arg != NULL; arg++) {
		launcherArgv.emplace_back(*arg);
	}

	/* todo: env_list */
	if (env_list) {
		fprintf(stderr, "not implemented: env_list\n");
	}

	/* optionally use file input */
	std::map<int, int> remapFds;
	if (stdin_fd  >= 0) { remapFds[stdin_fd] = STDIN_FILENO;  }
	if (stdout_fd >= 0) { remapFds[stdin_fd] = STDOUT_FILENO; }
	if (stderr_fd >= 0) { remapFds[stdin_fd] = STDERR_FILENO; }

	try {
		mpirInstances.emplace(id, new MPIRInstance(std::string(launcher), launcherArgv, {}, remapFds));
	} catch (...) {
		return INSTANCE_ERROR;
	}

	return id;
}

mpir_id_t _cti_mpir_newAttachInstance(const char *launcher, pid_t pid) {
	mpir_id_t id = newId();

	try {
		mpirInstances.emplace(id, new MPIRInstance(launcher, pid));
	} catch (...) {
		return INSTANCE_ERROR;
	}

	return id;
}

int _cti_mpir_releaseInstance(mpir_id_t id) {
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return 1; }

	DEBUG(std::cerr, "releasing id " << id << std::endl);

	mpirInstances.erase(it);
	return 0;
}

void _cti_mpir_releaseAllInstances(void) {
	mpirInstances.clear();
}

char* _cti_mpir_getStringAt(mpir_id_t id, const char *symbol) {
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return NULL; }

	return strdup(it->second->readStringAt(symbol).c_str());
}

cti_mpir_procTable_t* _cti_mpir_newProcTable(mpir_id_t id) {
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return NULL; }

	cti_mpir_procTable_t *procTable_C = new cti_mpir_procTable_t;

	/* get instance proctable */
	std::vector<MPIRInstance::MPIR_ProcTableElem> procTable_CXX = it->second->getProcTable();

	/* allocate subtables */
	procTable_C->num_pids  = procTable_CXX.size();
	procTable_C->pids      = new pid_t[procTable_C->num_pids];
	procTable_C->hostnames = new char*[procTable_C->num_pids];

	/* copy elements */
	for (size_t i = 0; i < procTable_C->num_pids; i++) {
		procTable_C->pids[i]      = procTable_CXX[i].pid;
		procTable_C->hostnames[i] = strdup(procTable_CXX[i].hostname.c_str());
	}

	return procTable_C;
}

void _cti_mpir_deleteProcTable(cti_mpir_procTable_t *proc_table) {
	if (proc_table == NULL) { return; }

	for (size_t i = 0; i < proc_table->num_pids; i++) {
		free(proc_table->hostnames[i]);
	}
	delete proc_table->hostnames;
	delete proc_table->pids;
	delete proc_table;
}

pid_t _cti_mpir_getLauncherPid(mpir_id_t id) {
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return 0; }

	return it->second->getLauncherPid();
}

/* fake stubs */
cti_gdb_id_t		_cti_gdb_newInstance(void) {
	throw std::runtime_error("called _cti_gdb_stub");
	return -1; }
void				_cti_gdb_cleanup(cti_gdb_id_t id) {
	throw std::runtime_error("called _cti_gdb_stub");
	return; }
void				_cti_gdb_cleanupAll(void) {
	throw std::runtime_error("called _cti_gdb_stub");
	return; }
void				_cti_gdb_execStarter(cti_gdb_id_t id, const char *a, const char *b, const char *c, const char * const d[], const char *e) {
	throw std::runtime_error("called _cti_gdb_stub");
	return; }
void				_cti_gdb_execAttach(cti_gdb_id_t id, const char *a, const char *b, pid_t c) { return; }
int					_cti_gdb_postFork(cti_gdb_id_t a) {
	throw std::runtime_error("called _cti_gdb_stub");
	return 1; }
char *				_cti_gdb_getSymbolVal(cti_gdb_id_t a, const char *b) { return NULL; }
cti_mpir_pid_t *	_cti_gdb_getAppPids(cti_gdb_id_t id) {
	throw std::runtime_error("called _cti_gdb_stub");
	return NULL; }
cti_mpir_proctable_t * _cti_gdb_getProctable(cti_gdb_id_t gdb_id) {
	throw std::runtime_error("called _cti_gdb_stub");
	return NULL; }
pid_t 				_cti_gdb_getLauncherPid(cti_gdb_id_t gdb_id) {
	throw std::runtime_error("called _cti_gdb_stub");
	return 0; }
void				_cti_gdb_freeMpirPid(cti_mpir_pid_t *idp) {
	throw std::runtime_error("called _cti_gdb_stub");
	return; }
int					_cti_gdb_release(cti_gdb_id_t id) {
	throw std::runtime_error("called _cti_gdb_stub");
	return 1; }
void 			_cti_gdb_freeProctable(cti_mpir_proctable_t *a) {
	throw std::runtime_error("called _cti_gdb_stub");
	return; }
