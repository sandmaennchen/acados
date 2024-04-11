# -*- coding: future_fstrings -*-
#
# Copyright (c) The acados authors.
#
# This file is part of acados.
#
# The 2-Clause BSD License
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.;
#
import numpy as np
from acados_template import AcadosOcpSolver
from sensitivity_utils import export_parametric_ocp

def main(qp_solver_ric_alg: int, use_cython=False):
    """
    Evaluate policy and calculate its gradient for the pendulum on a cart with a parametric model.
    """
    p_nominal = 1.0
    x0 = np.array([0.0, np.pi / 2, 0.0, 0.0])
    p_test = p_nominal - 0.2

    nx = len(x0)
    nu = 1

    N_horizon = 50
    T_horizon = 2.0
    Fmax = 80.0

    ocp = export_parametric_ocp(x0=x0, N_horizon=N_horizon, T_horizon=T_horizon, Fmax=Fmax, qp_solver_ric_alg=1)
    if use_cython:
        raise NotImplementedError()
        AcadosOcpSolver.generate(ocp, json_file="parameter_augmented_acados_ocp.json")
        AcadosOcpSolver.build(ocp.code_export_directory, with_cython=True)
        acados_ocp_solver = AcadosOcpSolver.create_cython_solver("parameter_augmented_acados_ocp.json")
    else:
        acados_ocp_solver = AcadosOcpSolver(ocp, json_file="parameter_augmented_acados_ocp.json")

    # create sensitivity solver
    ocp = export_parametric_ocp(x0=x0, N_horizon=N_horizon, T_horizon=T_horizon, Fmax=Fmax, hessian_approx='EXACT', qp_solver_ric_alg=qp_solver_ric_alg)
    ocp.model.name = 'sensitivity_solver'
    ocp.code_export_directory = f'c_generated_code_{ocp.model.name}'
    if use_cython:
        AcadosOcpSolver.generate(ocp, json_file=f"{ocp.model.name}.json")
        AcadosOcpSolver.build(ocp.code_export_directory, with_cython=True)
        sensitivity_solver = AcadosOcpSolver.create_cython_solver(f"{ocp.model.name}.json")
    else:
        sensitivity_solver = AcadosOcpSolver(ocp, json_file=f"{ocp.model.name}.json")

    p_val = np.array([p_test])

    for n in range(N_horizon+1):
        acados_ocp_solver.set(n, 'p', p_val)
        sensitivity_solver.set(n, 'p', p_val)

    u_opt = acados_ocp_solver.solve_for_x0(x0)[0]
    acados_ocp_solver.store_iterate(filename='iterate.json', overwrite=True, verbose=False)

    sensitivity_solver.load_iterate(filename='iterate.json', verbose=False)
    sensitivity_solver.solve_for_x0(x0, fail_on_nonzero_status=False, print_stats_on_failure=False)

    if sensitivity_solver.get_status() not in [0, 2]:
        breakpoint()

    # adjoint direction
    sens_x0_seed = np.zeros((1, nx))
    sens_x0_seed[0, 0] = 1
    sens_x1_seed = sens_x0_seed.copy()
    sens_u0_seed = np.zeros((1, nu))
    sens_u0_seed[0, 0] = 1
    sens_u1_seed = sens_u0_seed.copy()

    # Calculate the policy gradient
    sens_x_forw, sens_u_forw = sensitivity_solver.eval_solution_sensitivity([0, 1], "params_global")

    adj_p = sens_x0_seed @ sens_x_forw[0] + \
            sens_x1_seed @ sens_x_forw[1] + \
            sens_u0_seed @ sens_u_forw[0] + \
            sens_u1_seed @ sens_u_forw[1]

    # set adjoint seed
    sensitivity_solver.reset_sens_out()
    sensitivity_solver.set(0, 'sens_x', sens_x0_seed.flatten())
    sensitivity_solver.set(0, 'sens_u', sens_u0_seed.flatten())
    sensitivity_solver.set(1, 'sens_x', sens_x1_seed.flatten())

    # breakpoint()
    # TODO: evalute adjoint of solution sensitivity wrt params.
    # TODO: compare wrt adj_p

if __name__ == "__main__":
    main(qp_solver_ric_alg=0, use_cython=False)