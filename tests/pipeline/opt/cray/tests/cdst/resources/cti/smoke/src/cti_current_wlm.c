#include "common_tools_fe.h"
#include <stdio.h>

int main() {
     cti_wlm_type_t wlm = cti_current_wlm();

     switch (wlm) {
        case CTI_WLM_SLURM:
            printf("%s\n", CTI_WLM_TYPE_SLURM_STR);
            break;
        case CTI_WLM_ALPS:
            printf("%s\n", CTI_WLM_TYPE_ALPS_STR);
            break;
        case CTI_WLM_SSH:
            printf("%s\n", CTI_WLM_TYPE_SSH_STR);
            break;
        case CTI_WLM_PALS:
            printf("%s\n", CTI_WLM_TYPE_PALS_STR);
            break;
        case CTI_WLM_FLUX:
            printf("%s\n", CTI_WLM_TYPE_FLUX_STR);
            break;
        case CTI_WLM_LOCALHOST:
            printf("%s\n", CTI_WLM_TYPE_LOCALHOST_STR);
            break;
        case CTI_WLM_MOCK:
            printf("bad wlm: mock\n");
	    return 1;
        case CTI_WLM_NONE:
            printf("bad wlm: none\n");
            return 1;
     }

     return 0;
}
