#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <wait.h>
#include <limits.h>
#include <errno.h>

typedef int(*pals_init2_type)(void **state);
typedef int(*pals_fini_type)(void *state);
typedef int(*pals_start_barrier_type)(void *state);

void *libpals_handle = NULL;

pals_init2_type pals_init2 = NULL;
pals_fini_type pals_fini = NULL;
pals_start_barrier_type pals_start_barrier = NULL;

void *pals_state = NULL;

static void close_libpals()
{
	pals_state = NULL;

	pals_init2 = NULL;
	pals_fini = NULL;
	pals_start_barrier = NULL;

	if (libpals_handle != NULL) {
		dlclose(libpals_handle);
		libpals_handle = NULL;
	}
}

// Use pkg-config to detect the location of the libpals library,
// or use the system default directory upon failure.
static void*
dlopen_libpals()
{
	void *libpals_handle;
	char const *detected_path = NULL;
	char path[PATH_MAX];
	path[0] = '\0';

	int pkgconfig_pipe[2];
	pid_t pkgconfig_pid = -1;
	int read_cursor = 0;
	char libpals_libdir[PATH_MAX];

	// Try in ldcache / LD_LIBRARY_PATH
	libpals_handle = dlopen("libpals.so", RTLD_LAZY);
	if (libpals_handle != NULL) {
		return libpals_handle;
	}

	// Set up pkgconfig pipe
	if (pipe(pkgconfig_pipe) < 0) {
		perror("pipe");
		goto cleanup_detect_libpals;
	}

	// Fork pkgconfig
	pkgconfig_pid = fork();
	if (pkgconfig_pid < 0) {
		perror("fork");
		goto cleanup_detect_libpals;

	// Query pkgconfig for libpals' libdir
	} else if (pkgconfig_pid == 0) {
		char const* pkgconfig_argv[] = {"pkg-config", "--variable=libdir", "libpals", NULL};

		// Set up pkgconfig output
		close(pkgconfig_pipe[0]);
		pkgconfig_pipe[0] = -1;
		dup2(pkgconfig_pipe[1], STDOUT_FILENO);

		// Exec pkgconfig
		execvp("pkg-config", (char* const*)pkgconfig_argv);
		perror("execvp");
		exit(-1);
	}

	// Set up pkgconfig input
	close(pkgconfig_pipe[1]);
	pkgconfig_pipe[1] = -1;

	// Read pkgconfig output
	read_cursor = 0;
	while (1) {
		errno = 0;
		int read_rc = read(pkgconfig_pipe[0], libpals_libdir + read_cursor,
			sizeof(libpals_libdir) - read_cursor - 1);

		if (read_rc < 0) {

			// Retry if applicable
			if (errno == EINTR) {
				continue;

			} else {
				perror("read");
				goto cleanup_detect_libpals;
			}

		// Return result if EOF
		} else if (read_rc == 0) {

			// No data was read
			if (read_cursor == 0) {
				break;
			}

			// Remove trailing newline
			libpals_libdir[read_cursor - 1] = '\0';

			detected_path = libpals_libdir;
			break;

		// Update cursor with number of bytes read
		} else {
			read_cursor += read_rc;

			// pkgconfig output is larger than maximum path size
			if (read_cursor >= (sizeof(libpals_libdir) - 1)) {
				goto cleanup_detect_libpals;
			}
		}
	}

cleanup_detect_libpals:

	close(pkgconfig_pipe[0]);
	pkgconfig_pipe[0] = -1;

	// Wait and check for pkgconfig return code
	if (pkgconfig_pid > 0) {

		// Reset SIGCHLD to default
		int old_action_valid = 0;
		struct sigaction old_action;

		// Back up old signal disposition
		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;

		// Ignore invalid sigaction and let processing continue
		if (sigaction(SIGCHLD, &sa, &old_action) < 0) {
			perror("sigaction");
		} else {
			old_action_valid = 1;
		}

		// Wait for pkgconfig to exit
		while (1) {
			int status;

			// Get exit code
			errno = 0;
			if (waitpid(pkgconfig_pid, &status, 0) < 0) {

				// Retry wait if applicable
				if (errno == EAGAIN) {
					continue;

				// pkgconfig failed, use system default
				} else if (errno != ECHILD) {
					perror("waitpid");
					detected_path = NULL;
					break;

				} else {
					break;
				}
			}

			// Check exit code
			if (WEXITSTATUS(status)) {
				detected_path = NULL;
				break;
			}
		}

		// Restore old SIGCHLD disposition
		if (old_action_valid) {
			if (sigaction(SIGCHLD, &old_action, NULL) < 0) {
				perror("sigaction");
			}
			old_action_valid = 0;
		}
	}

	// Format detected path with libpals library and check for existence
	if (detected_path != NULL) {

		// Format path
		int snprintf_rc = snprintf(path, PATH_MAX,
			"%s/%s", detected_path, "libpals.so");
		path[PATH_MAX - 1] = '\0';
		if ((snprintf_rc < 0) || (snprintf_rc >= PATH_MAX)) {
			perror("snprintf");
			return NULL;
		}

		// Check for libpals library at path
		libpals_handle = dlopen(path, RTLD_LAZY);
		if (libpals_handle != NULL) {
			return libpals_handle;
		}
	}

	// Detection failed or libpals was not in detected directory
	// Check in default paths
	char const* pals_default_paths[] = {
		"/opt/cray/pe/pals/default/lib",
		"/opt/cray/pals/default/lib",
		"/usr/lib64",
		NULL
	};
	for (char const** default_path = pals_default_paths; *default_path != NULL; default_path++) {

		// Format path
		int snprintf_rc = snprintf(path, PATH_MAX,
			"%s/%s", *default_path, "libpals.so");
		path[PATH_MAX - 1] = '\0';
		if ((snprintf_rc < 0) || (snprintf_rc >= PATH_MAX)) {
			perror("snprintf");
			return NULL;
		}

		// Check for libpals library at path
		libpals_handle = dlopen(path, RTLD_LAZY);
		if (libpals_handle != NULL) {
			return libpals_handle;
		}
	}

	// Failed to automatically detect
	return NULL;
}

__attribute__((constructor))
void pals_init_constructor()
{
	char *error = NULL;
	char const *cti_save_ld_preload = NULL;

	// Restore original LD_PRELOAD VALUE
	unsetenv("LD_PRELOAD");
	if ((cti_save_ld_preload = getenv("CTI_SAVE_LD_PRELOAD")) != NULL) {
		setenv("LD_PRELOAD", cti_save_ld_preload, 1);
		unsetenv("CTI_SAVE_LD_PRELOAD");
	}

	libpals_handle = dlopen_libpals();
	if (libpals_handle == NULL) {
		fprintf(stderr, "Error loading libpals.so. Tool launch may fail.\n");
		goto pals_init_constructor_error;
	}

	pals_init2 = (pals_init2_type)dlsym(libpals_handle, "pals_init2");
	if ((error = dlerror()) != NULL) {
		fprintf(stderr, "Error loading pals_init2: %s\n", error);
		goto pals_init_constructor_error;
	} else if (pals_init2 == NULL) {
		fprintf(stderr, "Error loading pals_init2. Tool launch may fail.\n");
		goto pals_init_constructor_error;
	}

	pals_fini = (pals_fini_type)dlsym(libpals_handle, "pals_fini");
	if ((error = dlerror()) != NULL) {
		fprintf(stderr, "Error loading pals_fini: %s\n", error);
		goto pals_init_constructor_error;
	} else if (pals_fini == NULL) {
		fprintf(stderr, "Error loading pals_fini. Tool launch may fail.\n");
		goto pals_init_constructor_error;
	}

	pals_start_barrier = (pals_start_barrier_type)dlsym(libpals_handle, "pals_start_barrier");
	if ((error = dlerror()) != NULL) {
		fprintf(stderr, "Error loading pals_start_barrier: %s\n", error);
		goto pals_init_constructor_error;
	} else if (pals_start_barrier == NULL) {
		fprintf(stderr, "Error loading pals_start_barrier. Tool launch may fail.\n");
		goto pals_init_constructor_error;
	}

	if (pals_init2(&pals_state) == 0) {
		pals_start_barrier(pals_state);
	}

	return;

pals_init_constructor_error:
	close_libpals();
	return;
}

__attribute((destructor))
void pals_finalize_destructor()
{
	if (pals_fini != NULL) {
		pals_fini(pals_state);
	}

	close_libpals();
}
