import os
import re

def refactor_file(file_path):
    with open(file_path, 'r') as f:
        content = f.read()

    # Skip if types.h
    if 'types.h' in file_path:
        return

    # Replace double with real_t, but avoid replacing it in strings or already refactored parts
    # Using word boundaries to avoid replacing things like "double_value" (if any)
    new_content = re.sub(r'\bdouble\b', 'real_t', content)

    # Replace DBL_MAX with std::numeric_limits<real_t>::max()
    if 'DBL_MAX' in new_content:
        new_content = new_content.replace('DBL_MAX', 'std::numeric_limits<real_t>::max()')
        if '<limits>' not in new_content:
            # Add <limits> include
            if '#include' in new_content:
                new_content = re.sub(r'(#include\s+<[^>]+>)', r'#include <limits>\n\1', new_content, 1)
            else:
                new_content = '#include <limits>\n' + new_content

    # Replace DBL_MIN with std::numeric_limits<real_t>::min()
    if 'DBL_MIN' in new_content:
        new_content = new_content.replace('DBL_MIN', 'std::numeric_limits<real_t>::min()')
        if '<limits>' not in new_content:
            if '#include' in new_content:
                new_content = re.sub(r'(#include\s+<[^>]+>)', r'#include <limits>\n\1', new_content, 1)
            else:
                new_content = '#include <limits>\n' + new_content

    # If header file and real_t is used, ensure types.h is included
    if file_path.endswith('.h'):
        if 'real_t' in new_content and 'types.h' not in new_content:
            # Try to add it after other includes or at the top of namespace
            if '#include' in new_content:
                # Find last include
                includes = re.findall(r'#include\s+["<][^">]+[">]', new_content)
                if includes:
                    last_include = includes[-1]
                    new_content = new_content.replace(last_include, last_include + '\n#include "types.h"')
                else:
                    new_content = '#include "types.h"\n' + new_content
            else:
                # Add before namespace
                new_content = '#include "types.h"\n' + new_content

    # If source file, ensure "types.h" or the corresponding header is included
    # Actually most source files include their header which will now include types.h

    if new_content != content:
        with open(file_path, 'w') as f:
            f.write(new_content)
        print(f"Refactored {file_path}")

files_to_process = [
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/clutch_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/clutch_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/conjugate_gradient_sle_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/conjugate_gradient_sle_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/constant_rotation_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/constant_rotation_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/constant_speed_motor.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/constant_speed_motor.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/euler_ode_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/euler_ode_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/fixed_position_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/fixed_position_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/fixed_rotation_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/fixed_rotation_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/force_generator.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/force_generator.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/gauss_seidel_sle_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/gauss_seidel_sle_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/gaussian_elimination_sle_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/gaussian_elimination_sle_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/generic_rigid_body_system.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/generic_rigid_body_system.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/gravity_force_generator.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/gravity_force_generator.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/line_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/line_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/link_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/link_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/nsv_ode_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/nsv_ode_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/ode_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/ode_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/optimized_nsv_rigid_body_system.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/optimized_nsv_rigid_body_system.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/rigid_body_system.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/rigid_body_system.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/rk4_ode_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/rk4_ode_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/rolling_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/rolling_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/rotation_friction_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/rotation_friction_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/simple_gear_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/simple_gear_constraint.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/sle_solver.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/sle_solver.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/sparse_matrix.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/sparse_matrix.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/spring.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/spring.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/static_force_generator.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/static_force_generator.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/system_state.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/scs.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/line_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/link_constraint.h",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/include/rotation_friction_constraint.h",
]

# Also include test files
test_files = [
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/sparse_matrix_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/matrix_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/rolling_constraint_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/gaussian_elimination_sle_solver_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/conjugate_gradient_sle_solver_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/sanity_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/euler_ode_solver_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/rk4_ode_solver_tests.cpp",
    "engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/test/gauss_seidel_sle_solver_tests.cpp",
]

for f in files_to_process + test_files:
    if os.path.exists(f):
        refactor_file(f)
    else:
        print(f"File not found: {f}")
