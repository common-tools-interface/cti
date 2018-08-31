#include "Session.hpp"

// getpid
#include <sys/types.h>
#include <unistd.h>
// valid chars array used in seed generation
static const char _cti_valid_char[] {
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K','L','M',
	'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'a','b','c','d','e','f','g','h','i','j','k','l','m',
	'n','o','p','q','r','s','t','u','v','w','x','y','z' };
class CTIPRNG {
	char _cti_r_state[256];
public:
	CTIPRNG() {
		// set the PRNG state
		if (setstate((char *)_cti_r_state) == NULL) {
			throw std::runtime_error("setstate failed.");
		}

		// We need to generate a good seed to avoid collisions. Since this
		// library can be used by automated tests, it is vital to have a
		// good seed.
		struct timespec		tv;
		unsigned int		pval;
		unsigned int		seed;
		
		// get the current time from epoch with nanoseconds
		if (clock_gettime(CLOCK_REALTIME, &tv)) {
			throw std::runtime_error("clock_gettime failed.");
		}
		
		// generate an appropriate value from the pid, we shift this to
		// the upper 16 bits of the int. This should avoid problems with
		// collisions due to slight variations in nano time and adding in
		// pid offsets.
		pval = (unsigned int)getpid() << ((sizeof(unsigned int) * CHAR_BIT) - 16);
		
		// Generate the seed. This is not crypto safe, but should have enough
		// entropy to avoid the case where two procs are started at the same
		// time that use this interface.
		seed = (tv.tv_sec ^ tv.tv_nsec) + pval;
		
		// init the state
		initstate(seed, (char *)_cti_r_state, sizeof(_cti_r_state));
	}

	char genChar() {
		unsigned int oset;

		// Generate a random offset into the array. This is random() modded 
		// with the number of elements in the array.
		oset = random() % (sizeof(_cti_valid_char)/sizeof(_cti_valid_char[0]));
		// assing this char
		return _cti_valid_char[oset];
	}
};

std::string Session::generateStagePath() {
	std::string stagePath;

	// check to see if the caller set a staging directory name, otherwise generate one
	if (const char* customStagePath = getenv(DAEMON_STAGE_VAR)) {
		stagePath = customStagePath;
	} else {
		// remove placeholder Xs from DEFAULT_STAGE_DIR
		const std::string stageFormat(DEFAULT_STAGE_DIR);
		stagePath = stageFormat.substr(0, stageFormat.find("X"));

		// now start replacing the 'X' characters in the stage_name string with
		// randomness
		CTIPRNG prng;
		for (size_t i = 0; i < (stageFormat.length() - stagePath.length()); i++) {
			stagePath.push_back(prng.genChar());
		}
	}

	return stagePath;
}

std::weak_ptr<Manifest> Session::createManifest() {
	auto manifest = std::make_shared<Manifest>(std::shared_ptr<Session>(this));
	manifests.emplace_back(manifest);
	return std::weak_ptr<Manifest>(manifest);
}