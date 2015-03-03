/*
 * demangle.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include <cstring>
#include <cxxabi.h>

extern "C" {

int demangle(const char* name, char * out, unsigned long int size) {
	int status;
	char* res = abi::__cxa_demangle(name, out, &size, &status);
	if(status != 0) {
		strcpy(out, name);
	}
	return status;
}

}
