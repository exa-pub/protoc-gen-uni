/* Schema of google/protobuf/compiler/plugin.proto (CodeGeneratorRequest; the response is
 * written by hand). */
#include "schema.h"

extern const PbMsgDef pbd_Version;

static const PbField f_Version[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT), S(3, INT32, PL_OPT), S(4, STRING, PL_OPT)};
DEF(pbd_Version, f_Version);

static const PbField f_CodeGeneratorRequest[] = {
	S(1, STRING, PL_REP), S(2, STRING, PL_OPT), M(3, PL_OPT, pbd_Version),
	M(15, PL_REP, pbd_FileDescriptorProto), M(17, PL_REP, pbd_FileDescriptorProto),
};
DEF(pbd_CodeGeneratorRequest, f_CodeGeneratorRequest);
