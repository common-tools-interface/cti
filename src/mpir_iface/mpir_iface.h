#ifndef MPIR_IFACE_H_
#define MPIR_IFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

	typedef int mpir_id_t;

	typedef struct {
		size_t num_pids; //number of ranks
		pid_t* pids; //pid of each rank
		char** hostnames; //host corresponding to each rank of size num_pids
	} cti_mpir_procTable_t;

	/* function prototypes */
	mpir_id_t _cti_mpir_newLaunchInstance(const char *launcher, const char * const launcher_args[],
		const char *input_file);
	mpir_id_t _cti_mpir_newAttachInstance(const char *launcher, pid_t pid);

	int _cti_mpir_releaseInstance(mpir_id_t id);
	void _cti_mpir_releaseAllInstances(void);

	char* _cti_mpir_getStringAt(mpir_id_t id, const char *symbol);

	cti_mpir_procTable_t* _cti_mpir_newProcTable(mpir_id_t id);
	void _cti_mpir_deleteProcTable(cti_mpir_procTable_t *proc_table);

	pid_t _cti_mpir_getLauncherPid(mpir_id_t id);

	typedef int cti_gdb_id_t;


/* fake stubs for transistion */
typedef struct {
	size_t		num_pids;
	pid_t *		pid;
} cti_mpir_pid_t;
typedef struct {
	size_t		num_pids;
	pid_t *		pid;
} cti_pid_t;
typedef struct {
	size_t num_pids; //number of ranks
	pid_t* pids; //pid of each rank
	char** hostnames; //host corresponding to each rank of size num_pids
} cti_mpir_proctable_t;
cti_gdb_id_t		_cti_gdb_newInstance(void);
void				_cti_gdb_cleanup(cti_gdb_id_t);
void				_cti_gdb_cleanupAll(void);
void				_cti_gdb_execStarter(cti_gdb_id_t, const char *, const char *, const char *, const char * const[], const char *);
void				_cti_gdb_execAttach(cti_gdb_id_t, const char *, const char *, pid_t);
int					_cti_gdb_postFork(cti_gdb_id_t);
char *				_cti_gdb_getSymbolVal(cti_gdb_id_t, const char *);
cti_mpir_pid_t *	_cti_gdb_getAppPids(cti_gdb_id_t);
cti_mpir_proctable_t * _cti_gdb_getProctable(cti_gdb_id_t gdb_id);
pid_t 				_cti_gdb_getLauncherPid(cti_gdb_id_t gdb_id);
void				_cti_gdb_freeMpirPid(cti_mpir_pid_t *);
int					_cti_gdb_release(cti_gdb_id_t);
void 			_cti_gdb_freeProctable(cti_mpir_proctable_t *);

#ifdef __cplusplus
}
#endif

#endif