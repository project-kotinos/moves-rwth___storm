#include "gtest/gtest.h"

#include "src/solver/MinMaxLinearEquationSolver.h"

TEST( MinMaxMethod, Simple ) {
    storm::solver::MinMaxMethodSelection ts = storm::solver::MinMaxMethodSelection::PolicyIteration;
    storm::solver::MinMaxMethod t = storm::solver::MinMaxMethod::PolicyIteration;
       ASSERT_EQ(convert(ts), t);
    
    
}