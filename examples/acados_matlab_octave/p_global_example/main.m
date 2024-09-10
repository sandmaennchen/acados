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


function main()

    import casadi.*

    residuals_no_lut_ref = run_example_ocp(false, false);
    residuals_no_lut = run_example_ocp(false, true);

    if ~all(abs(residuals_no_lut_ref - residuals_no_lut) < 1e-10)
        error("Residuals with lut=false do not match.");
    end

    residuals_with_lut_ref = run_example_ocp(true, false);
    residuals_with_lut = run_example_ocp(true, true);

    if ~all(abs(residuals_with_lut_ref - residuals_with_lut) < 1e-10)
        error("Residuals with lut=true do not match.");
    end
end


function residuals = run_example_ocp(lut, use_p_global)

    import casadi.*

    fprintf('\n\nRunning example with lut=%d, use_p_global=%d\n', lut, use_p_global);

    % Create p_global parameters
    [p_global, m, l, C, p_global_values] = create_p_global(lut);

    % OCP formulation
    ocp = create_ocp_formulation(p_global, m, l, C, lut, use_p_global, p_global_values);

    % OCP solver
    ocp_solver = AcadosOcpSolver(ocp);

    % Solve
    residuals = [];

    if use_p_global
        ocp_solver.set_p_global(p_global_values);
    end

    for i = 1:20
        ocp_solver.solve();
        % TODO implement get_residuals()
        % residuals = [residuals; ocp_solver.get_residuals()];
        residuals = [residuals; ocp_solver.get('x')];
    end

    % Plot results
    PLOT = false;

    if PLOT
        utraj = ocp_solver.get('u');
        xtraj = ocp_solver.get('x');
        plot_pendulum(ocp.solver_options.shooting_nodes, xtraj, utraj);

        if is_octave()
            waitforbuttonpress;
        end
    end
end

function residuals = run_example_mocp(lut, use_p_global)
    % TODO
    residuals = 0;
end

function [p_global, m, l, C, p_global_values] = create_p_global(lut)

    import casadi.*
    m = MX.sym('m');
    l = MX.sym('l');
    p_global = {m, l};
    p_global_values = [0.1; 0.8];

    if lut
        data = rand(7, 6, 2); % Example data, replace with actual data
        C = MX.sym('C', numel(data), 1);
        p_global{end+1} = C;
        p_global_values = [p_global_values; data(:)];
    else
        C = [];
    end

    p_global = vertcat(p_global{:});
end


function plot_pendulum(shooting_nodes, xtraj, utraj)
    figure; hold on;
    states = {'p', 'theta', 'v', 'dtheta'};
    for i=1:length(states)
        subplot(length(states), 1, i);
        plot(shooting_nodes, xtraj(i,:)); grid on;
        ylabel(states{i});
        xlabel('t [s]')
    end

    figure
    stairs(shooting_nodes, [utraj'; utraj(end)])
    ylabel('F [N]')
    xlabel('t [s]')
    grid on

end