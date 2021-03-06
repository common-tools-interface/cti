/******************************************************************************\
 * cti_stack.h - Header file for the stack interface.
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

#ifndef _CTI_STACK_H
#define _CTI_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

#define CTI_DEFAULT_STACK   128

/* struct typedefs */
typedef struct
{
    unsigned int    idx;
    unsigned int    num_elems;
    void **         elems;
} cti_stack_t;

/* function prototypes */
cti_stack_t *   _cti_newStack(void);
void            _cti_consumeStack(cti_stack_t *);
int             _cti_push(cti_stack_t *, void *);
void *          _cti_pop(cti_stack_t *);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_STACK_H */
