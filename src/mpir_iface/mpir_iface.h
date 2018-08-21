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
		const char * const env_list[], int stdin_fd, int stdout_fd, int stderr_fd);
	mpir_id_t _cti_mpir_newAttachInstance(const char *launcher, pid_t pid);

	int _cti_mpir_releaseInstance(mpir_id_t id);
	void _cti_mpir_releaseAllInstances(void);

	char* _cti_mpir_getStringAt(mpir_id_t id, const char *symbol);

	cti_mpir_procTable_t* _cti_mpir_newProcTable(mpir_id_t id);
	void _cti_mpir_deleteProcTable(cti_mpir_procTable_t *proc_table);

	pid_t _cti_mpir_getLauncherPid(mpir_id_t id);

	typedef int cti_gdb_id_t;

#ifdef __cplusplus
}
#endif

#endif