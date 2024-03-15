"""
Test for solution sensitivities with many parameters.
"""

import os
import scipy
import numpy as np
from casadi import SX, norm_2, vertcat
import casadi as ca
from casadi.tools import struct_symSX, struct_SX, entry
from casadi.tools.structure3 import DMStruct
import matplotlib.pyplot as plt
from acados_template import AcadosModel, AcadosSim, AcadosSimSolver, AcadosOcp, AcadosOcpSolver

import tqdm

from typing import Tuple
from plot_utils import (
    plot_chain_control_traj,
    plot_chain_position_traj,
    plot_chain_velocity_traj,
    animate_chain_position,
)


def get_chain_params():
    """Get chain parameters."""
    params = {}

    params["n_mass"] = 5
    params["Ts"] = 0.2
    params["Tsim"] = 5
    params["N"] = 40
    params["u_init"] = np.array([-1, 1, 1])
    params["with_wall"] = True
    params["yPosWall"] = -0.05  # Dimitris: - 0.1;
    params["xPosFirstMass"] = np.zeros(3)
    params["m"] = 0.033  # mass of the balls
    params["D"] = 1.0  # spring constant
    params["L"] = 0.033  # rest length of spring
    params["C"] = 0.1  # damping constant
    params["perturb_scale"] = 1e-2

    # params["m"] = np.random.normal(params["m_nom"], params["perturb_scale"] * params["m_nom"], params["n_mass"])
    # params["D"] = np.random.normal(params["D_nom"], params["perturb_scale"] * params["m_nom"], (params["n_mass"], 3))
    # params["L"] = np.random.normal(params["L_nom"], params["perturb_scale"] * params["m_nom"], (params["n_mass"], 3))
    # params["C"] = np.random.normal(params["C_nom"], params["perturb_scale"] * params["m_nom"], (params["n_mass"], 3))

    params["save_results"] = True
    params["show_plots"] = True
    params["nlp_iter"] = 50
    params["seed"] = 50
    params["nlp_tol"] = 1e-5

    return params


# def export_erk4_integrator(model: AcadosModel, Ts: float) -> AcadosSimSolver:
def export_discrete_erk4_integrator_step(f_expl, x, u, p, h, n_stages: int = 2) -> ca.SX:
    dt = h / n_stages
    ode = ca.Function("f", [x, u, p], [f_expl])
    xnext = x
    for j in range(n_stages):
        k1 = ode(xnext, u, p)
        k2 = ode(xnext + dt / 2 * k1, u, p)
        k3 = ode(xnext + dt / 2 * k2, u, p)
        k4 = ode(xnext + dt * k3, u, p)
        xnext = xnext + dt / 6 * (k1 + 2 * k2 + 2 * k3 + k4)

    return xnext


def export_chain_mass_model(n_mass: int, Ts=0.2, disturbance=False) -> Tuple[AcadosModel, DMStruct]:
    """Export chain mass model for acados."""
    x0 = np.array([0, 0, 0])  # fix mass (at wall)

    M = n_mass - 2  # number of intermediate massesu

    nx = (2 * M + 1) * 3  # differential states
    nu = 3  # control inputs

    xpos = SX.sym("xpos", (M + 1) * 3, 1)  # position of fix mass eliminated
    xvel = SX.sym("xvel", M * 3, 1)
    u = SX.sym("u", nu, 1)
    xdot = SX.sym("xdot", nx, 1)

    f = SX.zeros(3 * M, 1)  # force on intermediate masses

    n_link = n_mass - 1

    param_enries = [
        entry("m", shape=1, repeat=n_link),
        entry("D", shape=3, repeat=n_link),
        entry("L", shape=3, repeat=n_link),
        entry("C", shape=3, repeat=n_link),
    ]

    if disturbance:
        param_enries.append(entry("w", shape=3, repeat=n_mass - 2))

    p = struct_symSX(param_enries)

    # Gravity force
    for i in range(M):
        f[3 * i + 2] = -9.81

    # Spring force
    for i in range(M + 1):
        if i == 0:
            dist = xpos[i * 3 : (i + 1) * 3] - x0
        else:
            dist = xpos[i * 3 : (i + 1) * 3] - xpos[(i - 1) * 3 : i * 3]

        F = ca.SX.zeros(3, 1)
        for j in range(3):
            F[j] = p["D", i, j] / p["m", i] * (1 - p["L", i, j] / norm_2(dist)) * dist[j]
        # F = p["D", i] / p["m", i] * (1 - p["L", i] / norm_2(dist)) * dist

        # mass on the right
        if i < M:
            f[i * 3 : (i + 1) * 3] -= F

        # mass on the left
        if i > 0:
            f[(i - 1) * 3 : i * 3] += F

    # Damping force
    for i in range(M + 1):
        if i == 0:
            vel = xvel[i * 3 : (i + 1) * 3]
        elif i == M:
            vel = u - xvel[(i - 1) * 3 : i * 3]
        else:
            vel = xvel[i * 3 : (i + 1) * 3] - xvel[(i - 1) * 3 : i * 3]

        F = ca.SX.zeros(3, 1)
        for j in range(3):
            F[j] = p["C", i, j] * vel[j]
        # F = p["C", i] * vel

        # mass on the right
        if i < M:
            f[i * 3 : (i + 1) * 3] -= F

        # mass on the left
        if i > 0:
            f[(i - 1) * 3 : i * 3] += F

    x = vertcat(xpos, xvel)

    # dynamics
    if disturbance:
        # Disturbance on intermediate masses
        for i in range(M):
            f[i * 3 : (i + 1) * 3] += p["w", i]
        model_name = "chain_mass_ds_" + str(n_mass)
    else:
        model_name = "chain_mass_" + str(n_mass)

    f_expl = vertcat(xvel, u, f)
    f_impl = xdot - f_expl
    f_disc = export_discrete_erk4_integrator_step(f_expl, x, u, p, Ts)

    model = AcadosModel()

    model.f_impl_expr = f_impl
    model.f_expl_expr = f_expl
    model.disc_dyn_expr = f_disc
    model.x = x
    model.xdot = xdot
    model.u = u
    model.p = p.cat
    model.name = model_name

    p_map = p(0)

    return model, p_map


def compute_steady_state(model: AcadosModel, p, xPosFirstMass, xEndRef):
    """Compute steady state for chain mass model."""

    p_ = p(0)
    p_["m"] = p["m"]
    p_["D"] = p["D"]
    p_["L"] = p["L"]
    p_["C"] = p["C"]

    nx = model.x.shape[0]

    # Free masses
    M = int((nx / 3 - 1) / 2)

    # initial guess for state
    pos0_x = np.linspace(xPosFirstMass[0], xEndRef[0], M + 2)
    x0 = np.zeros((nx, 1))
    x0[: 3 * (M + 1) : 3] = pos0_x[1:].reshape((M + 1, 1))

    # decision variables
    w = [model.x, model.xdot, model.u]

    # initial guess
    w0 = ca.vertcat(*[x0, np.zeros(model.xdot.shape), np.zeros(model.u.shape)])

    # constraints
    g = []
    g += [model.f_impl_expr]  # steady state
    g += [model.x[3 * M : 3 * (M + 1)] - xEndRef]  # fix position of last mass
    g += [model.u]  # don't actuate controlled mass

    # misuse IPOPT as nonlinear equation solver
    nlp = {"x": ca.vertcat(*w), "f": 0, "g": ca.vertcat(*g), "p": model.p}

    solver = ca.nlpsol("solver", "ipopt", nlp)
    sol = solver(x0=w0, lbg=0, ubg=0, p=p_.cat)

    wrest = sol["x"].full()
    xrest = wrest[:nx]

    return xrest


def sampleFromEllipsoid(w, Z):
    """
    draws uniform sample from ellipsoid with center w and variability matrix Z
    """

    n = w.shape[0]  # dimension
    lam, v = np.linalg.eig(Z)

    # sample in hypersphere
    r = np.random.rand() ** (1 / n)  # radial position of sample
    x = np.random.randn(n)
    x = x / np.linalg.norm(x)
    x *= r
    # project to ellipsoid
    y = v @ (np.sqrt(lam) * x) + w

    return y


def export_parametric_sim(chain_params_: dict, p_: DMStruct, disturbance: bool = True) -> AcadosSim:
    """Export chain mass integrator for acados."""
    sim = AcadosSim()
    # simulation options
    n_mass = chain_params_["n_mass"]
    Ts = chain_params_["Ts"]

    # export model
    model, _ = export_chain_mass_model(n_mass, disturbance=disturbance)

    # set model
    sim.model = model

    # disturbances
    # nparam = 3 * M
    sim.parameter_values = p_.cat.full().flatten()

    # solver options
    sim.solver_options.integrator_type = "IRK"

    sim.solver_options.num_stages = 2
    sim.solver_options.num_steps = 2
    # sim.solver_options.nlp_solver_tol_eq = 1e-9

    # set prediction horizon
    sim.solver_options.T = Ts

    return sim


def export_parametric_ocp(
    chain_params_: dict,
    qp_solver_ric_alg: int = 0,
    qp_solver: str = "PARTIAL_CONDENSING_HPIPM",
    hessian_approx: str = "GAUSS_NEWTON",
    integrator_type: str = "IRK",
    nlp_solver_type: str = "SQP",
    nlp_iter: int = 50,
    nlp_tol: float = 1e-5,
) -> Tuple[AcadosOcp, DMStruct]:
    # create ocp object to formulate the OCP
    ocp = AcadosOcp()

    # chain parameters
    n_mass = chain_params_["n_mass"]
    M = chain_params_["n_mass"] - 2  # number of intermediate masses
    Ts = chain_params_["Ts"]
    ocp.dims.N = chain_params_["N"]
    with_wall = chain_params_["with_wall"]
    yPosWall = chain_params_["yPosWall"]
    xPosFirstMass = chain_params_["xPosFirstMass"]

    # export model
    model, p = export_chain_mass_model(n_mass, Ts=Ts, disturbance=True)

    # set model
    ocp.model = model

    nx = model.x.size()[0]
    nu = model.u.size()[0]
    ny = nx + nu
    Tf = ocp.dims.N * Ts

    # initial state

    xEndRef = np.zeros((3, 1))
    xEndRef[0] = chain_params_["L"] * (M + 1) * 6
    xEndRef[2] = 0.0
    # xEndRef[0] = M * chain_params["L"]

    # parameters
    np.random.seed(chain_params_["seed"])
    m = [np.random.normal(chain_params_["m"], 0.1 * chain_params_["m"]) for _ in range(n_mass - 1)]
    D = [np.random.normal(chain_params_["D"], 0.1 * chain_params_["D"], 3) for _ in range(n_mass - 1)]
    L = [np.random.normal(chain_params_["L"], 0.1 * chain_params_["L"], 3) for _ in range(n_mass - 1)]
    C = [np.random.normal(chain_params_["C"], 0.1 * chain_params_["C"], 3) for _ in range(n_mass - 1)]

    # Inermediate masses
    for i_mass in range(n_mass - 1):
        p["m", i_mass] = m[i_mass]
    for i_mass in range(n_mass - 2):
        p["w", i_mass] = np.zeros(3)

    # Links
    for i_link in range(n_mass - 1):
        p["D", i_link] = D[i_link]
        p["L", i_link] = L[i_link]
        p["C", i_link] = C[i_link]

    # xrest = compute_steady_state(n_mass, p, xPosFirstMass, xEndRef, disturbance=True)
    xrest = compute_steady_state(model, p, xPosFirstMass, xEndRef)

    x0 = xrest

    plt.figure()
    plt.plot(x0[::3], x0[2::3], "o")
    plt.grid(True)
    plt.show()


    # set cost module
    ocp.cost.cost_type = "LINEAR_LS"
    ocp.cost.cost_type_e = "LINEAR_LS"

    Q = 2 * np.diagflat(np.ones((nx, 1)))
    q_diag = np.ones((nx, 1))
    strong_penalty = M + 1
    q_diag[3 * M] = strong_penalty
    q_diag[3 * M + 1] = strong_penalty
    q_diag[3 * M + 2] = strong_penalty
    Q = 2 * np.diagflat(q_diag)

    R = 2 * np.diagflat(1e-2 * np.ones((nu, 1)))

    ocp.cost.W = scipy.linalg.block_diag(Q, R)
    ocp.cost.W_e = Q

    ocp.cost.Vx = np.zeros((ny, nx))
    ocp.cost.Vx[:nx, :nx] = np.eye(nx)

    Vu = np.zeros((ny, nu))
    Vu[nx : nx + nu, :] = np.eye(nu)
    ocp.cost.Vu = Vu

    ocp.cost.Vx_e = np.eye(nx)

    yref = np.vstack((xrest, np.zeros((nu, 1)))).flatten()
    ocp.cost.yref = yref
    ocp.cost.yref_e = xrest.flatten()

    # set constraints
    umax = 1 * np.ones((nu,))

    ocp.constraints.lbu = -umax
    ocp.constraints.ubu = umax
    ocp.constraints.x0 = x0.reshape((nx,))
    ocp.constraints.idxbu = np.array(range(nu))

    ocp.parameter_values = p.cat.full().flatten()

    # wall constraint
    if with_wall:
        nbx = M + 1
        Jbx = np.zeros((nbx, nx))
        for i in range(nbx):
            Jbx[i, 3 * i + 1] = 1.0

        ocp.constraints.Jbx = Jbx
        ocp.constraints.lbx = yPosWall * np.ones((nbx,))
        ocp.constraints.ubx = 1e9 * np.ones((nbx,))

        # slacks
        ocp.constraints.Jsbx = np.eye(nbx)
        L2_pen = 1e3
        L1_pen = 1
        ocp.cost.Zl = L2_pen * np.ones((nbx,))
        ocp.cost.Zu = L2_pen * np.ones((nbx,))
        ocp.cost.zl = L1_pen * np.ones((nbx,))
        ocp.cost.zu = L1_pen * np.ones((nbx,))

    # solver options
    ocp.solver_options.qp_solver = qp_solver
    ocp.solver_options.hessian_approx = hessian_approx
    ocp.solver_options.integrator_type = integrator_type
    ocp.solver_options.nlp_solver_type = nlp_solver_type
    ocp.solver_options.sim_method_num_stages = 2
    ocp.solver_options.sim_method_num_steps = 2
    ocp.solver_options.qp_solver_ric_alg = qp_solver_ric_alg
    ocp.solver_options.qp_solver_cond_N = ocp.dims.N

    if hessian_approx == "EXACT":
        ocp.solver_options.nlp_solver_step_length = 0.0
        ocp.solver_options.nlp_solver_max_iter = 1
        ocp.solver_options.qp_solver_iter_max = 200
        ocp.solver_options.tol = 1e-10
    else:
        ocp.solver_options.nlp_solver_max_iter = 400
        ocp.solver_options.tol = 1e-8

    # ocp.solver_options.nlp_solver_tol_eq = 1e-9

    # set prediction horizon
    ocp.solver_options.tf = Tf

    return ocp, p


def main_simulation(chain_params_: dict):
    """Main simulation function."""

    ocp, parameter_values = export_parametric_ocp(chain_params_=chain_params_)

    ocp_json_file = "acados_ocp_" + ocp.model.name + ".json"

    ocp.solver_options.with_solution_sens_wrt_params = False


    # Check if json_file exists
    if os.path.exists(ocp_json_file):
        acados_ocp_solver = AcadosOcpSolver(ocp, json_file=ocp_json_file, build=False, generate=False)
    else:
        acados_ocp_solver = AcadosOcpSolver(ocp, json_file=ocp_json_file)

    sim = export_parametric_sim(chain_params_=chain_params_, p_=parameter_values)

    sim_json_file = "acados_sim_" + sim.model.name + ".json"

    # Check if json_file exists
    if os.path.exists(sim_json_file):
        acados_sim_solver = AcadosSimSolver(sim, json_file=sim_json_file, build=False, generate=False)
    else:
        acados_sim_solver = AcadosSimSolver(sim, json_file=sim_json_file, build=True, generate=True)

    # %% get initial state from xrest
    xcurrent = ocp.constraints.lbx_0.reshape((ocp.dims.nx,))
    for i in range(5):
        acados_sim_solver.set("x", xcurrent)
        acados_sim_solver.set("u", chain_params_["u_init"])

        status = acados_sim_solver.solve()
        if status != 0:
            raise Exception("acados integrator returned status {}. Exiting.".format(status))

        # update state
        xcurrent = acados_sim_solver.get("x")

    print(f"Initial state: {xcurrent}")

    Tsim = chain_params_["Tsim"]
    W = chain_params_["perturb_scale"] * np.eye(3)
    Ts = acados_sim_solver.acados_sim.solver_options.T
    nx = acados_sim_solver.acados_sim.model.x.size()[0]
    nu = acados_sim_solver.acados_sim.model.u.size()[0]
    n_mass = chain_params_["n_mass"]
    M = n_mass - 2
    # %% actual simulation
    N_sim = int(np.floor(Tsim / Ts))
    simX = np.ndarray((N_sim + 1, nx))
    simU = np.ndarray((N_sim, nu))
    wall_dist = np.zeros((N_sim,))

    timings = np.zeros((N_sim,))

    simX[0, :] = xcurrent

    # closed loop
    for i in range(N_sim):
        # solve ocp
        acados_ocp_solver.set(0, "lbx", xcurrent)
        acados_ocp_solver.set(0, "ubx", xcurrent)

        status = acados_ocp_solver.solve()
        timings[i] = acados_ocp_solver.get_stats("time_tot")

        if status != 0:
            raise Exception("acados acados_ocp_solver returned status {} in time step {}. Exiting.".format(status, i))

        simU[i, :] = acados_ocp_solver.get(0, "u")
        print("control at time", i, ":", simU[i, :])

        # simulate system
        acados_sim_solver.set("x", xcurrent)
        acados_sim_solver.set("u", simU[i, :])

        for i_mass in range(n_mass - 2):
            parameter_values["w", i_mass] = sampleFromEllipsoid(np.zeros(3), W)

        acados_sim_solver.set("p", parameter_values.cat.full().flatten())

        for stage in range(acados_ocp_solver.acados_ocp.dims.N + 1):
            acados_ocp_solver.set(stage, "p", parameter_values.cat.full().flatten())

        status = acados_sim_solver.solve()
        if status != 0:
            raise Exception("acados integrator returned status {}. Exiting.".format(status))

        # update state
        xcurrent = acados_sim_solver.get("x")
        simX[i + 1, :] = xcurrent

        # xOcpPredict = acados_ocp_solver.get(1, "x")
        # print("model mismatch = ", str(np.max(xOcpPredict - xcurrent)))
        yPos = xcurrent[range(1, 3 * M + 1, 3)]
        wall_dist[i] = np.min(yPos - chain_params_["yPosWall"])
        print("time i = ", str(i), " dist2wall ", str(wall_dist[i]))

    print("dist2wall (minimum over simulation) ", str(np.min(wall_dist)))

    if os.environ.get("ACADOS_ON_CI") is None and chain_params_["show_plots"]:
        plot_chain_control_traj(simU)
        plot_chain_position_traj(simX, yPosWall=chain_params_["yPosWall"])
        plot_chain_velocity_traj(simX)

        animate_chain_position(simX, chain_params_["xPosFirstMass"], yPosWall=chain_params_["yPosWall"])
        # animate_chain_position_3D(simX, xPosFirstMass)

        plt.show()


def main_parametric(
    qp_solver_ric_alg: int = 0, eigen_analysis: bool = False, chain_params_: dict = get_chain_params()
) -> None:
    ocp, parameter_values = export_parametric_ocp(chain_params_=chain_params_, qp_solver_ric_alg=qp_solver_ric_alg)

    ocp_json_file = "acados_ocp_" + ocp.model.name + ".json"

    # Check if json_file exists
    if os.path.exists(ocp_json_file):
        acados_ocp_solver = AcadosOcpSolver(ocp, json_file=ocp_json_file, build=False, generate=False)
    else:
        acados_ocp_solver = AcadosOcpSolver(ocp, json_file=ocp_json_file)

    sensitivity_ocp, _ = export_parametric_ocp(
        chain_params_=chain_params_, qp_solver_ric_alg=qp_solver_ric_alg, hessian_approx="EXACT", integrator_type="DISCRETE"
    )

    ocp_json_file = "acados_sensitivity_ocp_" + sensitivity_ocp.model.name + ".json"
    # Check if json_file exists
    if os.path.exists(ocp_json_file):
        sensitivity_solver = AcadosOcpSolver(sensitivity_ocp, json_file=ocp_json_file, build=False, generate=False)
    else:
        sensitivity_solver = AcadosOcpSolver(sensitivity_ocp, json_file=ocp_json_file)

    M = chain_params_["n_mass"] - 2
    xEndRef = np.zeros((3, 1))
    xEndRef[0] = chain_params_["L"] * (M + 1) * 6
    pos0_x = np.linspace(chain_params_["xPosFirstMass"][0], xEndRef[0], M + 2)
    x0 = np.zeros((ocp.dims.nx, 1))
    x0[: 3 * (M + 1) : 3] = pos0_x[1:].reshape((M + 1, 1))

    np_test = 50
    D_var = np.linspace(0.5 * chain_params_["D"], 1.5 * chain_params_["D"], np_test)
    delta_p = D_var[1] - D_var[0]

    sens_u = []
    pi = np.zeros(np_test)
    for i in tqdm.tqdm(range(np_test), desc="Sensitivity analysis"):
        parameter_values["D", -1, 0] = D_var[i]

        for stage in range(ocp.dims.N + 1):
            acados_ocp_solver.set(stage, "p", parameter_values.cat.full().flatten())
            sensitivity_solver.set(stage, "p", parameter_values.cat.full().flatten())

        pi[i] = acados_ocp_solver.solve_for_x0(x0)[0]
        acados_ocp_solver.store_iterate(filename="iterate.json", overwrite=True, verbose=False)
        sensitivity_solver.load_iterate(filename="iterate.json", verbose=False)
        sensitivity_solver.solve_for_x0(x0, fail_on_nonzero_status=False, print_stats_on_failure=False)

        residuals = sensitivity_solver.get_stats("residuals")
        print(f"residuals sensitivity_solver {residuals} status {sensitivity_solver.status}")

        if sensitivity_solver.status not in [0, 2]:
            print(f"warning; status = {sensitivity_solver.status}")
            breakpoint()

        # Calculate the policy gradient
        sens_x_, sens_u_ = sensitivity_solver.eval_solution_sensitivity(0, "params_global")

        # Check if sens_u contains NaN
        if np.isnan(sens_u_).any():
            print(f"warning; NaN in sens_u_ at i = {i}")
            breakpoint()

        sens_u.append(sens_u_)
        # sens_u[i] = sens_u_.item()

    # Compare to numerical gradients
    np_grad = np.gradient(pi, delta_p)
    pi_reconstructed_np_grad = np.cumsum(np_grad) * delta_p + pi[0]
    pi_reconstructed_np_grad += pi[0] - pi_reconstructed_np_grad[0]

    pi_reconstructed_acados = np.cumsum(sens_u) * delta_p + pi[0]
    pi_reconstructed_acados += pi[0] - pi_reconstructed_acados[0]


if __name__ == "__main__":
    main_simulation(chain_params_=get_chain_params())
    # main_parametric(qp_solver_ric_alg=0, eigen_analysis=False)
