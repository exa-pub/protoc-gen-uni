/* The explicit list of all generators, sorted by name and version. A new generator version is a
 * new directory generators/<owner>/<plugin>/<version>/ exporting one UniGenerator, listed here
 * and in generators/CMakeLists.txt. */
#include "registry.h"

extern const UniGenerator gen_bufbuild_es_v2_17_0;
extern const UniGenerator gen_protocolbuffers_go_v1_36_12;

const UniGenerator *const uni_generators[] = {
	&gen_bufbuild_es_v2_17_0,
	&gen_protocolbuffers_go_v1_36_12,
};

const int uni_n_generators = (int)(sizeof uni_generators / sizeof *uni_generators);
