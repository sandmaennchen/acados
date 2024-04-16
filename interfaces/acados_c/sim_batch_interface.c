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


#include "acados/sim/sim_common.h"
#include "acados/sim/sim_erk_integrator.h"
#include "acados/sim/sim_gnsf.h"
#include "acados/sim/sim_irk_integrator.h"
#include "acados/sim/sim_lifted_irk_integrator.h"

#include "acados_c/sim_batch_interface.h"
#include "acados_c/sim_interface.h"

// external
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "acados/utils/mem.h"


/************************************************
* solver
************************************************/

acados_size_t sim_batch_calculate_size(sim_config *config, void *dims, void *opts_, int N_batch)
{
    acados_size_t bytes = sizeof(sim_batch_solver);

    bytes += N_batch*config->memory_calculate_size(config, dims, opts_);
    bytes += N_batch*config->workspace_calculate_size(config, dims, opts_);

    return bytes;
}


sim_batch_solver *sim_batch_assign(sim_config *config, void *dims, void *opts_, void *raw_memory, int N_batch)
{
    char *c_ptr = (char *) raw_memory;

    sim_batch_solver *solver = (sim_batch_solver *) c_ptr;
    c_ptr += sizeof(sim_batch_solver);

    for (int i = 0; i < N_batch; i++)
    {
        solver->sim_solvers[i] = sim_assign(solver->sim_solvers[i]->config, solver->sim_solvers[i]->dims, solver->sim_solvers[i]->opts, raw_memory);
        c_ptr += sim_calculate_size(solver->sim_solvers[i]->config, solver->sim_solvers[i]->dims, solver->sim_solvers[i]->opts);
    }

    assert((char *) raw_memory + sim_batch_calculate_size(config, dims, opts_, N_batch) == c_ptr);

    return solver;
}



sim_batch_solver *sim_batch_solver_create(sim_config *config, void *dims, void *opts_, int N_batch)
{

    acados_size_t bytes = sim_batch_calculate_size(config, dims, opts_, N_batch);

    void *ptr = calloc(1, bytes);

    sim_batch_solver *solver = sim_batch_assign(config, dims, opts_, ptr, N_batch);

    for (int i = 0; i < solver->N_batch; i++)
    {
        printf("creating solver \n");
        solver->sim_solvers[i] = sim_solver_create(config, dims, opts_);
    }

    return solver;
}



void sim_batch_solver_destroy(void *solver_)
{
    sim_batch_solver * solver = solver_;

    for (int i = 0; i < solver->N_batch; i++)
    {
        sim_solver_destroy(solver->sim_solvers[i]);
    }

    // TODO
    free(solver);
}


int sim_batch_solver_sum_status(sim_batch_solver *solver)
{
    int status_all = 0;
    for (int i = 0; i < solver->N_batch; i++)
    {
        status_all += solver->status[i];
    }

    return status_all;
}

void sim_batch_solver_reset_status(sim_batch_solver *solver)
{
    for (int i = 0; i < solver->N_batch; i++)
    {
        solver->status[i] = 0;
    }
    return;
}

int sim_batch_solve(sim_batch_solver *solver, sim_in **in, sim_out ** out)
{

    sim_batch_solver_reset_status(solver);

    int status = 0;
#if defined(ACADOS_WITH_OPENMP)
    #pragma omp parallel for
#endif
    for (int i = 0; i < solver->N_batch; i++)
    {
        status = solver->sim_solvers[i]->config->evaluate(
            solver->sim_solvers[i]->config, in[i], out[i],
            solver->sim_solvers[i]->opts, solver->sim_solvers[i]->mem, solver->sim_solvers[i]->work);
    }
    return sim_batch_solver_sum_status(solver);
}

int sim_batch_precompute(sim_batch_solver *solver, sim_in **in, sim_out **out)
{

    for (int i = 0; i < solver->N_batch; i++)
    {
    solver->status[i] = solver->sim_solvers[i]->config->precompute(solver->sim_solvers[i]->config, in[i], out[i],
        solver->sim_solvers[i]->opts, solver->sim_solvers[i]->mem, solver->sim_solvers[i]->work);
    }

    return sim_batch_solver_sum_status(solver);
}


int sim_batch_solver_set(sim_batch_solver *solver, const char *field, void *value)
{

    for (int i = 0; i < solver->N_batch; i++)
    {
        solver->status[i] = solver->sim_solvers[i]->config->memory_set(solver->sim_solvers[i]->config, solver->sim_solvers[i]->dims, solver->sim_solvers[i]->mem, field, value);
    }

    return sim_batch_solver_sum_status(solver);
}
