// This file is part of "austin" which is released under GPL.
//
// See file LICENCE or go to http://www.gnu.org/licenses/ for full license
// details.
//
// Austin is a Python frame stack sampler for CPython.
//
// Copyright (c) 2018 Gabriele N. Tornetta <phoenix1987@gmail.com>.
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef LIBAUSTIN_H
#define LIBAUSTIN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>


typedef void (*austin_callback_t)(pid_t, pid_t);
typedef void * austin_handle_t;
typedef struct {
  uintptr_t      key;  // private
  char         * filename;
  char         * scope;
  unsigned int   line;
} austin_frame_t;


extern int
austin_up();

extern void
austin_down();

extern austin_handle_t
austin_attach(pid_t);

extern void
austin_detach(austin_handle_t);

extern int
austin_sample(austin_handle_t, austin_callback_t);

extern int
austin_sample_thread(austin_handle_t, pid_t);

extern austin_frame_t *
austin_pop_frame();

austin_frame_t *
austin_read_frame(austin_handle_t, void *);

#ifdef __cplusplus
}
#endif
#endif // LIBAUSTIN_H
