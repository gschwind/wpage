/*
 * demangle.h
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef SRC_DEMANGLE_H_
#define SRC_DEMANGLE_H_

#if __cplusplus
extern "C" {
#endif

int demangle(char const * name, char * out, unsigned long int size);

#if __cplusplus
}
#endif


#endif /* SRC_DEMANGLE_H_ */
