#ifndef CTI_SLURM_UTIL_H_
#define CTI_SLURM_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int cti_slurm_util_id_t;

typedef struct {
	char *			host;				// hostname of this node
	int				PEsHere;			// Number of PEs running on this node
	int				firstPE;			// First PE number on this node
} slurmNodeLayout_t;

typedef struct {
	int					numPEs;			// Number of PEs associated with the job step
	int					numNodes;		// Number of nodes associated with the job step
	slurmNodeLayout_t *	hosts;			// Array of hosts of length numNodes
} slurmStepLayout_t;

slurmStepLayout_t *_cti_cray_slurm_getLayout(uint32_t job_id, uint32_t step_id);
void				_cti_cray_slurm_freeLayout(slurmStepLayout_t *layout);

#ifdef __cplusplus
}
#endif

#endif