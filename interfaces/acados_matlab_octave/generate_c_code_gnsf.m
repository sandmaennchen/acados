%
% Copyright (c) The acados authors.
%
% This file is part of acados.
%
% The 2-Clause BSD License
%
% Redistribution and use in source and binary forms, with or without
% modification, are permitted provided that the following conditions are met:
%
% 1. Redistributions of source code must retain the above copyright notice,
% this list of conditions and the following disclaimer.
%
% 2. Redistributions in binary form must reproduce the above copyright notice,
% this list of conditions and the following disclaimer in the documentation
% and/or other materials provided with the distribution.
%
% THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
% AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
% IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
% ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
% LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
% CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
% SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
% INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
% CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
% ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
% POSSIBILITY OF SUCH DAMAGE.;

%


function generate_c_code_gnsf(model, opts, model_dir)

import casadi.*

casadi_opts = struct('mex', false, 'casadi_int', 'int', 'casadi_real', 'double');
check_casadi_version();

A = model.gnsf.A;
B = model.gnsf.B;
C = model.gnsf.C;
E = model.gnsf.E;
c = model.gnsf.c;

L_x    = model.gnsf.L_x;
L_z    = model.gnsf.L_z;
L_xdot = model.gnsf.L_xdot;
L_u    = model.gnsf.L_u;

A_LO = model.gnsf.A_LO;
E_LO = model.gnsf.E_LO;
B_LO = model.gnsf.B_LO;
c_LO = model.gnsf.c_LO;

% state permutation vector: x_gnsf = dvecpe(x, ipiv)
ipiv_x = model.gnsf.ipiv_x;
idx_perm_x = model.gnsf.idx_perm_x;
ipiv_z = model.gnsf.ipiv_z;
idx_perm_z = model.gnsf.idx_perm_z;
ipiv_f = model.gnsf.ipiv_f;
idx_perm_f = model.gnsf.idx_perm_f;

% expressions
phi = model.gnsf.expr_phi;
f_lo = model.gnsf.expr_f_lo;

% binaries
nontrivial_f_LO = model.gnsf.nontrivial_f_LO;
purely_linear = model.gnsf.purely_linear;

% symbolics
% y
y = model.gnsf.y;
% uhat
uhat = model.gnsf.uhat;
% general
x = model.x;
xdot = model.xdot;
u = model.u;
z = model.z;
p = model.p;

model_name = model.name;

nx1 = size(L_x, 2);
nz1 = size(L_z, 2);

% CasADi variables and expressions
x1 = x(idx_perm_x(1:nx1));
x1dot = xdot(idx_perm_x(1:nx1));
z1 = z(idx_perm_z(1:nz1));

return_dir = pwd;
cd(model_dir)

%% generate functions
if ~purely_linear
    jac_phi_y = jacobian(phi,y);
    jac_phi_uhat = jacobian(phi,uhat);

    phi_fun = Function([model_name,'_gnsf_phi_fun'], {y, uhat, p}, {phi});
    phi_fun_jac_y = Function([model_name,'_gnsf_phi_fun_jac_y'], {y, uhat, p}, {phi, jac_phi_y});
    phi_jac_y_uhat = Function([model_name,'_gnsf_phi_jac_y_uhat'], {y, uhat, p}, {jac_phi_y, jac_phi_uhat});

    phi_fun.generate([model_name,'_gnsf_phi_fun'], casadi_opts);
    phi_fun_jac_y.generate([model_name,'_gnsf_phi_fun_jac_y'], casadi_opts);
    phi_jac_y_uhat.generate([model_name,'_gnsf_phi_jac_y_uhat'], casadi_opts);

    if nontrivial_f_LO
        f_lo_fun_jac_x1k1uz = Function([model_name,'_gnsf_f_lo_fun_jac_x1k1uz'], {x1, x1dot, z1, u, p}, ...
            {f_lo, [jacobian(f_lo,x1), jacobian(f_lo,x1dot), jacobian(f_lo,u), jacobian(f_lo,z1)]});
        f_lo_fun_jac_x1k1uz.generate([model_name,'_gnsf_f_lo_fun_jac_x1k1uz'], casadi_opts);
    end
end

% get_matrices function
dummy = x(1);
get_matrices_fun = Function([model_name,'_gnsf_get_matrices_fun'], {dummy},...
     {A, B, C, E, L_x, L_xdot, L_z, L_u, A_LO, c, E_LO, B_LO,...
      nontrivial_f_LO, purely_linear, ipiv_x, ipiv_z, c_LO});
get_matrices_fun.generate([model_name,'_gnsf_get_matrices_fun'], casadi_opts);

cd(return_dir)

end
