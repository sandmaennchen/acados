/*
 * Copyright (c) The acados authors.
 *
 * This file is part of acados.
 *
 * The 2-Clause BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.;
 */


#ifndef INTERFACES_ACADOS_C_SIM_BATCH_INTERFACE_H_
#define INTERFACES_ACADOS_C_SIM_BATCH_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "acados/sim/sim_common.h"
#include "sim_interface.h"


typedef struct
{
    int N_batch;  // batch size
    int * status;
    sim_solver ** sim_solvers;

} sim_batch_solver;

/* solver */
//
ACADOS_SYMBOL_EXPORT acados_size_t sim_batch_calculate_size(sim_config *config, void *dims, void *opts_, int N_batch);
//
ACADOS_SYMBOL_EXPORT sim_batch_solver *sim_batch_assign(sim_config *config, void *dims, void *opts_, void *raw_memory, int N_batch);
//
ACADOS_SYMBOL_EXPORT sim_batch_solver *sim_batch_solver_create(sim_config *config, void *dims, void *opts_, int N_batch);
//
ACADOS_SYMBOL_EXPORT void sim_batch_solver_destroy(void *solver);
//
ACADOS_SYMBOL_EXPORT int sim_batch_solve(sim_batch_solver *solver, sim_in **in, sim_out **out);
//
ACADOS_SYMBOL_EXPORT int sim_batch_precompute(sim_batch_solver *solver, sim_in **in, sim_out **out);
//
ACADOS_SYMBOL_EXPORT int sim_batch_solver_set(sim_batch_solver *solver, const char *field, void *value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // INTERFACES_ACADOS_C_SIM_BATCH_INTERFACE_H_
