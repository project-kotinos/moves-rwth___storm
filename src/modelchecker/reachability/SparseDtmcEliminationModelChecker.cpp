#include "src/modelchecker/reachability/SparseDtmcEliminationModelChecker.h"

#include <algorithm>
#include <chrono>

#include "src/adapters/CarlAdapter.h"

#include "src/storage/StronglyConnectedComponentDecomposition.h"

#include "src/modelchecker/results/ExplicitQualitativeCheckResult.h"
#include "src/modelchecker/results/ExplicitQuantitativeCheckResult.h"

#include "src/utility/graph.h"
#include "src/utility/vector.h"
#include "src/utility/macros.h"

#include "src/exceptions/InvalidPropertyException.h"
#include "src/exceptions/InvalidStateException.h"
#include "exceptions/UnexpectedException.h"
#include "modelchecker/prctl/SparseDtmcPrctlModelChecker.h"

namespace storm {
    namespace modelchecker {
        
        template<typename ValueType>
        SparseDtmcEliminationModelChecker<ValueType>::SparseDtmcEliminationModelChecker(storm::models::sparse::Dtmc<ValueType> const& model) : model(model) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        bool SparseDtmcEliminationModelChecker<ValueType>::canHandle(storm::logic::Formula const& formula) const {
            if (formula.isProbabilityOperatorFormula()) {
                storm::logic::ProbabilityOperatorFormula const& probabilityOperatorFormula = formula.asProbabilityOperatorFormula();
                return this->canHandle(probabilityOperatorFormula.getSubformula());
            } else if (formula.isRewardOperatorFormula()) {
                storm::logic::RewardOperatorFormula const& rewardOperatorFormula = formula.asRewardOperatorFormula();
                return this->canHandle(rewardOperatorFormula.getSubformula());
            } else if (formula.isUntilFormula() || formula.isEventuallyFormula()) {
                if (formula.isUntilFormula()) {
                    storm::logic::UntilFormula const& untilFormula = formula.asUntilFormula();
                    if (untilFormula.getLeftSubformula().isPropositionalFormula() && untilFormula.getRightSubformula().isPropositionalFormula()) {
                        return true;
                    }
                } else if (formula.isEventuallyFormula()) {
                    storm::logic::EventuallyFormula const& eventuallyFormula = formula.asEventuallyFormula();
                    if (eventuallyFormula.getSubformula().isPropositionalFormula()) {
                        return true;
                    }
                }
            } else if (formula.isReachabilityRewardFormula()) {
                storm::logic::ReachabilityRewardFormula reachabilityRewardFormula = formula.asReachabilityRewardFormula();
                if (reachabilityRewardFormula.getSubformula().isPropositionalFormula()) {
                    return true;
                }
            } else if (formula.isConditionalPathFormula()) {
                storm::logic::ConditionalPathFormula conditionalPathFormula = formula.asConditionalPathFormula();
                if (conditionalPathFormula.getLeftSubformula().isEventuallyFormula() && conditionalPathFormula.getRightSubformula().isEventuallyFormula()) {
                    return this->canHandle(conditionalPathFormula.getLeftSubformula()) && this->canHandle(conditionalPathFormula.getRightSubformula());
                }
            } else if (formula.isPropositionalFormula()) {
                return true;
            }
            return false;
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> SparseDtmcEliminationModelChecker<ValueType>::computeUntilProbabilities(storm::logic::UntilFormula const& pathFormula, bool qualitative, boost::optional<storm::logic::OptimalityType> const& optimalityType) {
            // Retrieve the appropriate bitvectors by model checking the subformulas.
            std::unique_ptr<CheckResult> leftResultPointer = this->check(pathFormula.getLeftSubformula());
            std::unique_ptr<CheckResult> rightResultPointer = this->check(pathFormula.getRightSubformula());
            storm::storage::BitVector const& phiStates = leftResultPointer->asExplicitQualitativeCheckResult().getTruthValuesVector();
            storm::storage::BitVector const& psiStates = rightResultPointer->asExplicitQualitativeCheckResult().getTruthValuesVector();
            
            // Do some sanity checks to establish some required properties.
            STORM_LOG_THROW(model.getInitialStates().getNumberOfSetBits() == 1, storm::exceptions::IllegalArgumentException, "Input model is required to have exactly one initial state.");
            storm::storage::sparse::state_type initialState = *model.getInitialStates().begin();
            
            // Then, compute the subset of states that has a probability of 0 or 1, respectively.
            std::pair<storm::storage::BitVector, storm::storage::BitVector> statesWithProbability01 = storm::utility::graph::performProb01(model, phiStates, psiStates);
            storm::storage::BitVector statesWithProbability0 = statesWithProbability01.first;
            storm::storage::BitVector statesWithProbability1 = statesWithProbability01.second;
            storm::storage::BitVector maybeStates = ~(statesWithProbability0 | statesWithProbability1);
            
            // If the initial state is known to have either probability 0 or 1, we can directly return the result.
            if (model.getInitialStates().isDisjointFrom(maybeStates)) {
                STORM_LOG_DEBUG("The probability of all initial states was found in a preprocessing step.");
                return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, statesWithProbability0.get(*model.getInitialStates().begin()) ? storm::utility::zero<ValueType>() : storm::utility::one<ValueType>()));
            }
            
            // Determine the set of states that is reachable from the initial state without jumping over a target state.
            storm::storage::BitVector reachableStates = storm::utility::graph::getReachableStates(model.getTransitionMatrix(), model.getInitialStates(), maybeStates, statesWithProbability1);
            
            // Subtract from the maybe states the set of states that is not reachable (on a path from the initial to a target state).
            maybeStates &= reachableStates;
            
            // Create a vector for the probabilities to go to a state with probability 1 in one step.
            std::vector<ValueType> oneStepProbabilities = model.getTransitionMatrix().getConstrainedRowSumVector(maybeStates, statesWithProbability1);
            
            // Determine the set of initial states of the sub-model.
            storm::storage::BitVector newInitialStates = model.getInitialStates() % maybeStates;
            
            // We then build the submatrix that only has the transitions of the maybe states.
            storm::storage::SparseMatrix<ValueType> submatrix = model.getTransitionMatrix().getSubmatrix(false, maybeStates, maybeStates);
            storm::storage::SparseMatrix<ValueType> submatrixTransposed = submatrix.transpose();
            
            // Before starting the model checking process, we assign priorities to states so we can use them to
            // impose ordering constraints later.
            std::vector<std::size_t> statePriorities = getStatePriorities(submatrix, submatrixTransposed, newInitialStates, oneStepProbabilities);
            
            boost::optional<std::vector<ValueType>> missingStateRewards;
            return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, computeReachabilityValue(submatrix, oneStepProbabilities, submatrixTransposed, newInitialStates, phiStates, psiStates, missingStateRewards, statePriorities)));
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> SparseDtmcEliminationModelChecker<ValueType>::computeReachabilityRewards(storm::logic::ReachabilityRewardFormula const& rewardPathFormula, bool qualitative, boost::optional<storm::logic::OptimalityType> const& optimalityType) {
            // Retrieve the appropriate bitvectors by model checking the subformulas.
            std::unique_ptr<CheckResult> subResultPointer = this->check(rewardPathFormula.getSubformula());
            storm::storage::BitVector phiStates(model.getNumberOfStates(), true);
            storm::storage::BitVector const& psiStates = subResultPointer->asExplicitQualitativeCheckResult().getTruthValuesVector();
            
            // Do some sanity checks to establish some required properties.
            STORM_LOG_THROW(model.hasStateRewards() || model.hasTransitionRewards(), storm::exceptions::IllegalArgumentException, "Input model does not have a reward model.");
            STORM_LOG_THROW(model.getInitialStates().getNumberOfSetBits() == 1, storm::exceptions::IllegalArgumentException, "Input model is required to have exactly one initial state.");
            storm::storage::sparse::state_type initialState = *model.getInitialStates().begin();
            
            // Then, compute the subset of states that has a reachability reward less than infinity.
            storm::storage::BitVector trueStates(model.getNumberOfStates(), true);
            storm::storage::BitVector infinityStates = storm::utility::graph::performProb1(model.getBackwardTransitions(), trueStates, psiStates);
            infinityStates.complement();
            storm::storage::BitVector maybeStates = ~psiStates & ~infinityStates;
            
            // If the initial state is known to have 0 reward or an infinite reward value, we can directly return the result.
            if (infinityStates.get(initialState)) {
                STORM_LOG_DEBUG("The reward of all initial states was found in a preprocessing step.");
                // This is a work around, because not all (e.g. storm::RationalFunction) data types can represent an
                // infinity value.
                return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<double>(initialState, storm::utility::infinity<double>()));
            }
            if (psiStates.get(initialState)) {
                STORM_LOG_DEBUG("The reward of all initial states was found in a preprocessing step.");
                return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, storm::utility::zero<ValueType>()));
            }
            
            // Determine the set of states that is reachable from the initial state without jumping over a target state.
            storm::storage::BitVector reachableStates = storm::utility::graph::getReachableStates(model.getTransitionMatrix(), model.getInitialStates(), maybeStates, psiStates);
            
            // Subtract from the maybe states the set of states that is not reachable (on a path from the initial to a target state).
            maybeStates &= reachableStates;
            
            // Create a vector for the probabilities to go to a state with probability 1 in one step.
            std::vector<ValueType> oneStepProbabilities = model.getTransitionMatrix().getConstrainedRowSumVector(maybeStates, psiStates);
            
            // Determine the set of initial states of the sub-model.
            storm::storage::BitVector newInitialStates = model.getInitialStates() % maybeStates;
            
            // We then build the submatrix that only has the transitions of the maybe states.
            storm::storage::SparseMatrix<ValueType> submatrix = model.getTransitionMatrix().getSubmatrix(false, maybeStates, maybeStates);
            storm::storage::SparseMatrix<ValueType> submatrixTransposed = submatrix.transpose();
            
            // Before starting the model checking process, we assign priorities to states so we can use them to
            // impose ordering constraints later.
            std::vector<std::size_t> statePriorities = getStatePriorities(submatrix, submatrixTransposed, newInitialStates, oneStepProbabilities);
            
            // Project the state reward vector to all maybe-states.
            boost::optional<std::vector<ValueType>> optionalStateRewards(maybeStates.getNumberOfSetBits());
            std::vector<ValueType>& stateRewards = optionalStateRewards.get();
            if (model.hasTransitionRewards()) {
                // If a transition-based reward model is available, we initialize the right-hand
                // side to the vector resulting from summing the rows of the pointwise product
                // of the transition probability matrix and the transition reward matrix.
                std::vector<ValueType> pointwiseProductRowSumVector = model.getTransitionMatrix().getPointwiseProductRowSumVector(model.getTransitionRewardMatrix());
                storm::utility::vector::selectVectorValues(stateRewards, maybeStates, pointwiseProductRowSumVector);
                
                if (model.hasStateRewards()) {
                    // If a state-based reward model is also available, we need to add this vector
                    // as well. As the state reward vector contains entries not just for the states
                    // that we still consider (i.e. maybeStates), we need to extract these values
                    // first.
                    std::vector<ValueType> subStateRewards(stateRewards.size());
                    storm::utility::vector::selectVectorValues(subStateRewards, maybeStates, model.getStateRewardVector());
                    storm::utility::vector::addVectors(stateRewards, subStateRewards, stateRewards);
                }
            } else {
                // If only a state-based reward model is  available, we take this vector as the
                // right-hand side. As the state reward vector contains entries not just for the
                // states that we still consider (i.e. maybeStates), we need to extract these values
                // first.
                storm::utility::vector::selectVectorValues(stateRewards, maybeStates, model.getStateRewardVector());
            }
            
            return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, computeReachabilityValue(submatrix, oneStepProbabilities, submatrixTransposed, newInitialStates, phiStates, psiStates, optionalStateRewards, statePriorities)));
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> SparseDtmcEliminationModelChecker<ValueType>::computeConditionalProbabilities(storm::logic::ConditionalPathFormula const& pathFormula, bool qualitative, boost::optional<storm::logic::OptimalityType> const& optimalityType) {
            std::chrono::high_resolution_clock::time_point totalTimeStart = std::chrono::high_resolution_clock::now();
            
            // Retrieve the appropriate bitvectors by model checking the subformulas.
            STORM_LOG_THROW(pathFormula.getLeftSubformula().isEventuallyFormula(), storm::exceptions::InvalidPropertyException, "Expected 'eventually' formula.");
            STORM_LOG_THROW(pathFormula.getRightSubformula().isEventuallyFormula(), storm::exceptions::InvalidPropertyException, "Expected 'eventually' formula.");
            
            std::unique_ptr<CheckResult> leftResultPointer = this->check(pathFormula.getLeftSubformula().asEventuallyFormula().getSubformula());
            std::unique_ptr<CheckResult> rightResultPointer = this->check(pathFormula.getRightSubformula().asEventuallyFormula().getSubformula());
            storm::storage::BitVector phiStates = leftResultPointer->asExplicitQualitativeCheckResult().getTruthValuesVector();
            storm::storage::BitVector psiStates = rightResultPointer->asExplicitQualitativeCheckResult().getTruthValuesVector();
            storm::storage::BitVector trueStates(model.getNumberOfStates(), true);
            
            // Do some sanity checks to establish some required properties.
            // STORM_LOG_WARN_COND(storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationMethod() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationMethod::State, "The chosen elimination method is not available for computing conditional probabilities. Falling back to regular state elimination.");
            STORM_LOG_THROW(model.getInitialStates().getNumberOfSetBits() == 1, storm::exceptions::IllegalArgumentException, "Input model is required to have exactly one initial state.");
            storm::storage::sparse::state_type initialState = *model.getInitialStates().begin();
            
            storm::storage::SparseMatrix<ValueType> backwardTransitions = model.getBackwardTransitions();
            
            // Compute the 'true' psi states, i.e. those psi states that can be reached without passing through another psi state first.
            psiStates = storm::utility::graph::getReachableStates(model.getTransitionMatrix(), model.getInitialStates(), trueStates, psiStates) & psiStates;
            
            std::pair<storm::storage::BitVector, storm::storage::BitVector> statesWithProbability01 = storm::utility::graph::performProb01(backwardTransitions, trueStates, psiStates);
            storm::storage::BitVector statesWithProbabilityGreater0 = ~statesWithProbability01.first;
            storm::storage::BitVector statesWithProbability1 = std::move(statesWithProbability01.second);
            
            STORM_LOG_THROW(model.getInitialStates().isSubsetOf(statesWithProbabilityGreater0), storm::exceptions::InvalidPropertyException, "The condition of the conditional probability has zero probability.");
            
            // If the initial state is known to have probability 1 of satisfying the condition, we can apply regular model checking.
            if (model.getInitialStates().isSubsetOf(statesWithProbability1)) {
                STORM_LOG_INFO("The condition holds with probability 1, so the regular reachability probability is computed.");
                std::shared_ptr<storm::logic::BooleanLiteralFormula> trueFormula = std::make_shared<storm::logic::BooleanLiteralFormula>(true);
                std::shared_ptr<storm::logic::UntilFormula> untilFormula = std::make_shared<storm::logic::UntilFormula>(trueFormula, pathFormula.getLeftSubformula().asSharedPointer());
                return this->computeUntilProbabilities(*untilFormula);
            }
            
            // From now on, we know the condition does not have a trivial probability in the initial state.
            
            // Compute the states that can be reached on a path that has a psi state in it.
            storm::storage::BitVector statesWithPsiPredecessor = storm::utility::graph::performProbGreater0(model.getTransitionMatrix(), trueStates, psiStates);
            storm::storage::BitVector statesReachingPhi = storm::utility::graph::performProbGreater0(backwardTransitions, trueStates, phiStates);
            
            // The set of states we need to consider are those that have a non-zero probability to satisfy the condition or are on some path that has a psi state in it.
            STORM_LOG_TRACE("Initial state: " << model.getInitialStates());
            STORM_LOG_TRACE("Phi states: " << phiStates);
            STORM_LOG_TRACE("Psi state: " << psiStates);
            STORM_LOG_TRACE("States with probability greater 0 of satisfying the condition: " << statesWithProbabilityGreater0);
            STORM_LOG_TRACE("States with psi predecessor: " << statesWithPsiPredecessor);
            STORM_LOG_TRACE("States reaching phi: " << statesReachingPhi);
            storm::storage::BitVector maybeStates = statesWithProbabilityGreater0 | (statesWithPsiPredecessor & statesReachingPhi);
            STORM_LOG_TRACE("Found " << maybeStates.getNumberOfSetBits() << " relevant states: " << maybeStates);
            
            // Determine the set of initial states of the sub-DTMC.
            storm::storage::BitVector newInitialStates = model.getInitialStates() % maybeStates;
            STORM_LOG_TRACE("Found new initial states: " << newInitialStates << " (old: " << model.getInitialStates() << ")");

            // Create a dummy vector for the one-step probabilities.
            std::vector<ValueType> oneStepProbabilities(maybeStates.getNumberOfSetBits(), storm::utility::zero<ValueType>());
            
            // We then build the submatrix that only has the transitions of the maybe states.
            storm::storage::SparseMatrix<ValueType> submatrix = model.getTransitionMatrix().getSubmatrix(false, maybeStates, maybeStates);
            storm::storage::SparseMatrix<ValueType> submatrixTransposed = submatrix.transpose();
            
            // The states we want to eliminate are those that are tagged with "maybe" but are not a phi or psi state.
            phiStates = phiStates % maybeStates;
            
            // If there are no phi states in the reduced model, the conditional probability is trivially zero.
            if (phiStates.empty()) {
                return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, storm::utility::zero<ValueType>()));
            }
            
            psiStates = psiStates % maybeStates;
            
            // Keep only the states that we do not eliminate in the maybe states.
            maybeStates = phiStates | psiStates;
            
            STORM_LOG_TRACE("Phi states in reduced model " << phiStates);
            STORM_LOG_TRACE("Psi states in reduced model " << psiStates);
            storm::storage::BitVector statesToEliminate = ~maybeStates & ~newInitialStates;
            STORM_LOG_TRACE("Eliminating the states " << statesToEliminate);
            
            // Before starting the model checking process, we assign priorities to states so we can use them to
            // impose ordering constraints later.
            std::vector<std::size_t> statePriorities = getStatePriorities(submatrix, submatrixTransposed, newInitialStates, oneStepProbabilities);
            
            std::vector<storm::storage::sparse::state_type> states(statesToEliminate.begin(), statesToEliminate.end());
            
            // Sort the states according to the priorities.
            std::sort(states.begin(), states.end(), [&statePriorities] (storm::storage::sparse::state_type const& a, storm::storage::sparse::state_type const& b) { return statePriorities[a] < statePriorities[b]; });
            
            STORM_LOG_INFO("Computing conditional probilities." << std::endl);
            STORM_LOG_INFO("Eliminating " << states.size() << " states using the state elimination technique." << std::endl);
            boost::optional<std::vector<ValueType>> missingStateRewards;
            std::chrono::high_resolution_clock::time_point conversionStart = std::chrono::high_resolution_clock::now();
            FlexibleSparseMatrix flexibleMatrix = getFlexibleSparseMatrix(submatrix);
            FlexibleSparseMatrix flexibleBackwardTransitions = getFlexibleSparseMatrix(submatrixTransposed, true);
            std::chrono::high_resolution_clock::time_point conversionEnd = std::chrono::high_resolution_clock::now();
            std::chrono::high_resolution_clock::time_point modelCheckingStart = std::chrono::high_resolution_clock::now();
            for (auto const& state : states) {
                eliminateState(flexibleMatrix, oneStepProbabilities, state, flexibleBackwardTransitions, missingStateRewards);
            }
            STORM_LOG_INFO("Eliminated " << states.size() << " states." << std::endl);
            
            // Eliminate the transitions going into the initial state (if there are any).
            if (!flexibleBackwardTransitions.getRow(*newInitialStates.begin()).empty()) {
                eliminateState(flexibleMatrix, oneStepProbabilities, *newInitialStates.begin(), flexibleBackwardTransitions, missingStateRewards, false);
            }
            
            // Now we need to basically eliminate all chains of not-psi states after phi states and chains of not-phi
            // states after psi states.
            for (auto const& trans1 : flexibleMatrix.getRow(*newInitialStates.begin())) {
                auto initialStateSuccessor = trans1.getColumn();
                
                STORM_LOG_TRACE("Exploring successor " << initialStateSuccessor << " of the initial state.");
                
                if (phiStates.get(initialStateSuccessor)) {
                    STORM_LOG_TRACE("Is a phi state.");
                    
                    // If the state is both a phi and a psi state, we do not need to eliminate chains.
                    if (psiStates.get(initialStateSuccessor)) {
                        continue;
                    }
                    
                    // At this point, we know that the state satisfies phi and not psi.
                    // This means, we must compute the probability to reach psi states, which in turn means that we need
                    // to eliminate all chains of non-psi states between the current state and psi states.
                    bool hasNonPsiSuccessor = true;
                    while (hasNonPsiSuccessor) {
                        hasNonPsiSuccessor = false;
                        
                        // Only treat the state if it has an outgoing transition other than a self-loop.
                        auto const currentRow = flexibleMatrix.getRow(initialStateSuccessor);
                        if (currentRow.size() > 1 || (!currentRow.empty() && currentRow.front().getColumn() != initialStateSuccessor)) {
                            for (auto const& element : currentRow) {
                                // If any of the successors is a phi state, we eliminate it (wrt. all its phi predecessors).
                                if (!psiStates.get(element.getColumn())) {
                                    typename FlexibleSparseMatrix::row_type const& successorRow = flexibleMatrix.getRow(element.getColumn());
                                    // Eliminate the successor only if there possibly is a psi state reachable through it.
                                    if (successorRow.size() > 1 || (!successorRow.empty() && successorRow.front().getColumn() != element.getColumn())) {
                                        STORM_LOG_TRACE("Found non-psi successor " << element.getColumn() << " that needs to be eliminated.");
                                        eliminateState(flexibleMatrix, oneStepProbabilities, element.getColumn(), flexibleBackwardTransitions, missingStateRewards, false, true, phiStates);
                                        hasNonPsiSuccessor = true;
                                    }
                                }
                            }
                            STORM_LOG_ASSERT(!flexibleMatrix.getRow(initialStateSuccessor).empty(), "(1) New transitions expected to be non-empty.");
                        }
                    }
                } else {
                    STORM_LOG_ASSERT(psiStates.get(initialStateSuccessor), "Expected psi state.");
                    STORM_LOG_TRACE("Is a psi state.");

                    // At this point, we know that the state satisfies psi and not phi.
                    // This means, we must compute the probability to reach phi states, which in turn means that we need
                    // to eliminate all chains of non-phi states between the current state and phi states.
                    
                    bool hasNonPhiSuccessor = true;
                    while (hasNonPhiSuccessor) {
                        hasNonPhiSuccessor = false;
                        
                        // Only treat the state if it has an outgoing transition other than a self-loop.
                        auto const currentRow = flexibleMatrix.getRow(initialStateSuccessor);
                        if (currentRow.size() > 1 || (!currentRow.empty() && currentRow.front().getColumn() != initialStateSuccessor)) {
                            for (auto const& element : currentRow) {
                                // If any of the successors is a psi state, we eliminate it (wrt. all its psi predecessors).
                                if (!phiStates.get(element.getColumn())) {
                                    typename FlexibleSparseMatrix::row_type const& successorRow = flexibleMatrix.getRow(element.getColumn());
                                    if (successorRow.size() > 1 || (!successorRow.empty() && successorRow.front().getColumn() != element.getColumn())) {
                                        STORM_LOG_TRACE("Found non-phi successor " << element.getColumn() << " that needs to be eliminated.");
                                        eliminateState(flexibleMatrix, oneStepProbabilities, element.getColumn(), flexibleBackwardTransitions, missingStateRewards, false, true, psiStates);
                                        hasNonPhiSuccessor = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            ValueType numerator = storm::utility::zero<ValueType>();
            ValueType denominator = storm::utility::zero<ValueType>();
            
            for (auto const& trans1 : flexibleMatrix.getRow(*newInitialStates.begin())) {
                auto initialStateSuccessor = trans1.getColumn();
                if (phiStates.get(initialStateSuccessor)) {
                    if (psiStates.get(initialStateSuccessor)) {
                        numerator += trans1.getValue();
                        denominator += trans1.getValue();
                    } else {
                        ValueType additiveTerm = storm::utility::zero<ValueType>();
                        for (auto const& trans2 : flexibleMatrix.getRow(initialStateSuccessor)) {
                            if (psiStates.get(trans2.getColumn())) {
                                additiveTerm += trans2.getValue();
                            }
                        }
                        additiveTerm *= trans1.getValue();
                        numerator += additiveTerm;
                        denominator += additiveTerm;
                    }
                } else {
                    STORM_LOG_ASSERT(psiStates.get(initialStateSuccessor), "Expected psi state.");
                    denominator += trans1.getValue();
                    ValueType additiveTerm = storm::utility::zero<ValueType>();
                    for (auto const& trans2 : flexibleMatrix.getRow(initialStateSuccessor)) {
                        if (phiStates.get(trans2.getColumn())) {
                            additiveTerm += trans2.getValue();
                        }
                    }
                    numerator += trans1.getValue() * additiveTerm;
                }
            }
            std::chrono::high_resolution_clock::time_point modelCheckingEnd = std::chrono::high_resolution_clock::now();
            std::chrono::high_resolution_clock::time_point totalTimeEnd = std::chrono::high_resolution_clock::now();
            
            if (storm::settings::generalSettings().isShowStatisticsSet()) {
                std::chrono::high_resolution_clock::duration conversionTime = conversionEnd - conversionStart;
                std::chrono::milliseconds conversionTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(conversionTime);
                std::chrono::high_resolution_clock::duration modelCheckingTime = modelCheckingEnd - modelCheckingStart;
                std::chrono::milliseconds modelCheckingTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(modelCheckingTime);
                std::chrono::high_resolution_clock::duration totalTime = totalTimeEnd - totalTimeStart;
                std::chrono::milliseconds totalTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(totalTime);
                
                STORM_PRINT_AND_LOG(std::endl);
                STORM_PRINT_AND_LOG("Time breakdown:" << std::endl);
                STORM_PRINT_AND_LOG("    * time for conversion: " << conversionTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG("    * time for checking: " << modelCheckingTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG("------------------------------------------" << std::endl);
                STORM_PRINT_AND_LOG("    * total time: " << totalTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG(std::endl);
            }
            
            return std::unique_ptr<CheckResult>(new ExplicitQuantitativeCheckResult<ValueType>(initialState, numerator / denominator));
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> SparseDtmcEliminationModelChecker<ValueType>::checkBooleanLiteralFormula(storm::logic::BooleanLiteralFormula const& stateFormula) {
            if (stateFormula.isTrueFormula()) {
                return std::unique_ptr<CheckResult>(new ExplicitQualitativeCheckResult(storm::storage::BitVector(model.getNumberOfStates(), true)));
            } else {
                return std::unique_ptr<CheckResult>(new ExplicitQualitativeCheckResult(storm::storage::BitVector(model.getNumberOfStates())));
            }
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> SparseDtmcEliminationModelChecker<ValueType>::checkAtomicLabelFormula(storm::logic::AtomicLabelFormula const& stateFormula) {
            STORM_LOG_THROW(model.hasLabel(stateFormula.getLabel()), storm::exceptions::InvalidPropertyException, "The property refers to unknown label '" << stateFormula.getLabel() << "'.");
            return std::unique_ptr<CheckResult>(new ExplicitQualitativeCheckResult(model.getStates(stateFormula.getLabel())));
        }
        
        template<typename ValueType>
        ValueType SparseDtmcEliminationModelChecker<ValueType>::computeReachabilityValue(storm::storage::SparseMatrix<ValueType> const& transitionMatrix, std::vector<ValueType>& oneStepProbabilities, storm::storage::SparseMatrix<ValueType> const& backwardTransitions, storm::storage::BitVector const& initialStates, storm::storage::BitVector const& phiStates, storm::storage::BitVector const& psiStates, boost::optional<std::vector<ValueType>>& stateRewards, boost::optional<std::vector<std::size_t>> const& statePriorities) {
            std::chrono::high_resolution_clock::time_point totalTimeStart = std::chrono::high_resolution_clock::now();
            
            // Create a bit vector that represents the subsystem of states we still have to eliminate.
            storm::storage::BitVector subsystem = storm::storage::BitVector(transitionMatrix.getRowCount(), true);
            
            std::chrono::high_resolution_clock::time_point conversionStart = std::chrono::high_resolution_clock::now();
            // Then, we convert the reduced matrix to a more flexible format to be able to perform state elimination more easily.
            FlexibleSparseMatrix flexibleMatrix = getFlexibleSparseMatrix(transitionMatrix);
            FlexibleSparseMatrix flexibleBackwardTransitions = getFlexibleSparseMatrix(backwardTransitions, true);
            auto conversionEnd = std::chrono::high_resolution_clock::now();
            
            std::chrono::high_resolution_clock::time_point modelCheckingStart = std::chrono::high_resolution_clock::now();
            uint_fast64_t maximalDepth = 0;
            if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationMethod() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationMethod::State) {
                // If we are required to do pure state elimination, we simply create a vector of all states to
                // eliminate and sort it according to the given priorities.
                
                // Remove the initial state from the states which we need to eliminate.
                subsystem &= ~initialStates;
                std::vector<storm::storage::sparse::state_type> states(subsystem.begin(), subsystem.end());
                
                if (statePriorities) {
                    std::sort(states.begin(), states.end(), [&statePriorities] (storm::storage::sparse::state_type const& a, storm::storage::sparse::state_type const& b) { return statePriorities.get()[a] < statePriorities.get()[b]; });
                }
                
                STORM_LOG_DEBUG("Eliminating " << states.size() << " states using the state elimination technique." << std::endl);
                for (auto const& state : states) {
                    eliminateState(flexibleMatrix, oneStepProbabilities, state, flexibleBackwardTransitions, stateRewards);
                }
                STORM_LOG_DEBUG("Eliminated " << states.size() << " states." << std::endl);
            } else if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationMethod() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationMethod::Hybrid) {
                // When using the hybrid technique, we recursively treat the SCCs up to some size.
                std::vector<storm::storage::sparse::state_type> entryStateQueue;
                STORM_LOG_DEBUG("Eliminating " << subsystem.size() << " states using the hybrid elimination technique." << std::endl);
                maximalDepth = treatScc(flexibleMatrix, oneStepProbabilities, initialStates, subsystem, transitionMatrix, flexibleBackwardTransitions, false, 0, storm::settings::sparseDtmcEliminationModelCheckerSettings().getMaximalSccSize(), entryStateQueue, stateRewards, statePriorities);
                
                // If the entry states were to be eliminated last, we need to do so now.
                STORM_LOG_DEBUG("Eliminating " << entryStateQueue.size() << " entry states as a last step.");
                if (storm::settings::sparseDtmcEliminationModelCheckerSettings().isEliminateEntryStatesLastSet()) {
                    for (auto const& state : entryStateQueue) {
                        eliminateState(flexibleMatrix, oneStepProbabilities, state, flexibleBackwardTransitions, stateRewards);
                    }
                }
                STORM_LOG_DEBUG("Eliminated " << subsystem.size() << " states." << std::endl);
            }
            
            // Finally eliminate initial state.
            if (!stateRewards) {
                // If we are computing probabilities, then we can simply call the state elimination procedure. It
                // will scale the transition row of the initial state with 1/(1-loopProbability).
                STORM_LOG_INFO("Eliminating initial state " << *initialStates.begin() << "." << std::endl);
                eliminateState(flexibleMatrix, oneStepProbabilities, *initialStates.begin(), flexibleBackwardTransitions, stateRewards);
            } else {
                // If we are computing rewards, we cannot call the state elimination procedure for technical reasons.
                // Instead, we need to get rid of a potential loop in this state explicitly.
                
                // Start by finding the self-loop element. Since it can only be the only remaining outgoing transition
                // of the initial state, this amounts to checking whether the outgoing transitions of the initial
                // state are non-empty.
                if (!flexibleMatrix.getRow(*initialStates.begin()).empty()) {
                    STORM_LOG_ASSERT(flexibleMatrix.getRow(*initialStates.begin()).size() == 1, "At most one outgoing transition expected at this point, but found more.");
                    STORM_LOG_ASSERT(flexibleMatrix.getRow(*initialStates.begin()).front().getColumn() == *initialStates.begin(), "Remaining entry should be a self-loop, but it is not.");
                    ValueType loopProbability = flexibleMatrix.getRow(*initialStates.begin()).front().getValue();
                    loopProbability = storm::utility::one<ValueType>() / (storm::utility::one<ValueType>() - loopProbability);
                    STORM_LOG_DEBUG("Scaling the reward of the initial state " << stateRewards.get()[(*initialStates.begin())] << " with " << loopProbability);
                    stateRewards.get()[(*initialStates.begin())] *= loopProbability;
                    flexibleMatrix.getRow(*initialStates.begin()).clear();
                }
            }
            
            // Make sure that we have eliminated all transitions from the initial state.
            STORM_LOG_ASSERT(flexibleMatrix.getRow(*initialStates.begin()).empty(), "The transitions of the initial states are non-empty.");
            
            std::chrono::high_resolution_clock::time_point modelCheckingEnd = std::chrono::high_resolution_clock::now();
            std::chrono::high_resolution_clock::time_point totalTimeEnd = std::chrono::high_resolution_clock::now();
            
            if (storm::settings::generalSettings().isShowStatisticsSet()) {
                std::chrono::high_resolution_clock::duration conversionTime = conversionEnd - conversionStart;
                std::chrono::milliseconds conversionTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(conversionTime);
                std::chrono::high_resolution_clock::duration modelCheckingTime = modelCheckingEnd - modelCheckingStart;
                std::chrono::milliseconds modelCheckingTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(modelCheckingTime);
                std::chrono::high_resolution_clock::duration totalTime = totalTimeEnd - totalTimeStart;
                std::chrono::milliseconds totalTimeInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(totalTime);
                
                STORM_PRINT_AND_LOG(std::endl);
                STORM_PRINT_AND_LOG("Time breakdown:" << std::endl);
                STORM_PRINT_AND_LOG("    * time for conversion: " << conversionTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG("    * time for checking: " << modelCheckingTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG("------------------------------------------" << std::endl);
                STORM_PRINT_AND_LOG("    * total time: " << totalTimeInMilliseconds.count() << "ms" << std::endl);
                STORM_PRINT_AND_LOG(std::endl);
                STORM_PRINT_AND_LOG("Other:" << std::endl);
                STORM_PRINT_AND_LOG("    * number of states eliminated: " << transitionMatrix.getRowCount() << std::endl);
                if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationMethod() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationMethod::Hybrid) {
                    STORM_PRINT_AND_LOG("    * maximal depth of SCC decomposition: " << maximalDepth << std::endl);
                }
            }

            // Now, we return the value for the only initial state.
            STORM_LOG_DEBUG("Simplifying and returning result.");
            if (stateRewards) {
                return storm::utility::simplify(stateRewards.get()[*initialStates.begin()]);
            } else {
                return oneStepProbabilities[*initialStates.begin()];
            }
        }
        
        template<typename ValueType>
        std::vector<std::size_t> SparseDtmcEliminationModelChecker<ValueType>::getStatePriorities(storm::storage::SparseMatrix<ValueType> const& transitionMatrix, storm::storage::SparseMatrix<ValueType> const& transitionMatrixTransposed, storm::storage::BitVector const& initialStates, std::vector<ValueType> const& oneStepProbabilities) {
            std::vector<std::size_t> statePriorities(transitionMatrix.getRowCount());
            std::vector<std::size_t> states(transitionMatrix.getRowCount());
            for (std::size_t index = 0; index < states.size(); ++index) {
                states[index] = index;
            }
            if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::Random) {
                std::random_shuffle(states.begin(), states.end());
            } else {
                std::vector<std::size_t> distances;
                if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::Forward || storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::ForwardReversed) {
                    distances = storm::utility::graph::getDistances(transitionMatrix, initialStates);
                } else if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::Backward || storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::BackwardReversed) {
                    // Since the target states were eliminated from the matrix already, we construct a replacement by
                    // treating all states that have some non-zero probability to go to a target state in one step.
                    storm::storage::BitVector pseudoTargetStates(transitionMatrix.getRowCount());
                    for (std::size_t index = 0; index < oneStepProbabilities.size(); ++index) {
                        if (!comparator.isZero(oneStepProbabilities[index])) {
                            pseudoTargetStates.set(index);
                        }
                    }
                    
                    distances = storm::utility::graph::getDistances(transitionMatrixTransposed, pseudoTargetStates);
                } else {
                    STORM_LOG_ASSERT(false, "Illegal sorting order selected.");
                }
                
                // In case of the forward or backward ordering, we can sort the states according to the distances.
                if (storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::Forward || storm::settings::sparseDtmcEliminationModelCheckerSettings().getEliminationOrder() == storm::settings::modules::SparseDtmcEliminationModelCheckerSettings::EliminationOrder::Backward) {
                    std::sort(states.begin(), states.end(), [&distances] (storm::storage::sparse::state_type const& state1, storm::storage::sparse::state_type const& state2) { return distances[state1] < distances[state2]; } );
                } else {
                    // Otherwise, we sort them according to descending distances.
                    std::sort(states.begin(), states.end(), [&distances] (storm::storage::sparse::state_type const& state1, storm::storage::sparse::state_type const& state2) { return distances[state1] > distances[state2]; } );
                }
            }
            
            // Now convert the ordering of the states to priorities.
            for (std::size_t index = 0; index < states.size(); ++index) {
                statePriorities[states[index]] = index;
            }
            
            return statePriorities;
        }
        
        template<typename ValueType>
        uint_fast64_t SparseDtmcEliminationModelChecker<ValueType>::treatScc(FlexibleSparseMatrix& matrix, std::vector<ValueType>& oneStepProbabilities, storm::storage::BitVector const& entryStates, storm::storage::BitVector const& scc, storm::storage::SparseMatrix<ValueType> const& forwardTransitions, FlexibleSparseMatrix& backwardTransitions, bool eliminateEntryStates, uint_fast64_t level, uint_fast64_t maximalSccSize, std::vector<storm::storage::sparse::state_type>& entryStateQueue, boost::optional<std::vector<ValueType>>& stateRewards, boost::optional<std::vector<std::size_t>> const& statePriorities) {
            uint_fast64_t maximalDepth = level;
            
            // If the SCCs are large enough, we try to split them further.
            if (scc.getNumberOfSetBits() > maximalSccSize) {
                STORM_LOG_TRACE("SCC is large enough (" << scc.getNumberOfSetBits() << " states) to be decomposed further.");
                
                // Here, we further decompose the SCC into sub-SCCs.
                storm::storage::StronglyConnectedComponentDecomposition<ValueType> decomposition(forwardTransitions, scc & ~entryStates, false, false);
                STORM_LOG_TRACE("Decomposed SCC into " << decomposition.size() << " sub-SCCs.");
                
                // Store a bit vector of remaining SCCs so we can be flexible when it comes to the order in which
                // we eliminate the SCCs.
                storm::storage::BitVector remainingSccs(decomposition.size(), true);
                
                // First, get rid of the trivial SCCs.
                std::vector<std::pair<storm::storage::sparse::state_type, uint_fast64_t>> trivialSccs;
                for (uint_fast64_t sccIndex = 0; sccIndex < decomposition.size(); ++sccIndex) {
                    storm::storage::StronglyConnectedComponent const& scc = decomposition.getBlock(sccIndex);
                    if (scc.isTrivial()) {
                        storm::storage::sparse::state_type onlyState = *scc.begin();
                        trivialSccs.emplace_back(onlyState, sccIndex);
                    }
                }
                
                // If we are given priorities, sort the trivial SCCs accordingly.
                if (statePriorities) {
                    std::sort(trivialSccs.begin(), trivialSccs.end(), [&statePriorities] (std::pair<storm::storage::sparse::state_type, uint_fast64_t> const& a, std::pair<storm::storage::sparse::state_type, uint_fast64_t> const& b) { return statePriorities.get()[a.first] < statePriorities.get()[b.first]; });
                }
                
                STORM_LOG_TRACE("Eliminating " << trivialSccs.size() << " trivial SCCs.");
                for (auto const& stateIndexPair : trivialSccs) {
                    eliminateState(matrix, oneStepProbabilities, stateIndexPair.first, backwardTransitions, stateRewards);
                    remainingSccs.set(stateIndexPair.second, false);
                }
                STORM_LOG_TRACE("Eliminated all trivial SCCs.");
                
                // And then recursively treat the remaining sub-SCCs.
                STORM_LOG_TRACE("Eliminating " << remainingSccs.getNumberOfSetBits() << " remaining SCCs on level " << level << ".");
                for (auto sccIndex : remainingSccs) {
                    storm::storage::StronglyConnectedComponent const& newScc = decomposition.getBlock(sccIndex);
                    
                    // Rewrite SCC into bit vector and subtract it from the remaining states.
                    storm::storage::BitVector newSccAsBitVector(forwardTransitions.getRowCount(), newScc.begin(), newScc.end());
                    
                    // Determine the set of entry states of the SCC.
                    storm::storage::BitVector entryStates(forwardTransitions.getRowCount());
                    for (auto const& state : newScc) {
                        for (auto const& predecessor : backwardTransitions.getRow(state)) {
                            if (predecessor.getValue() != storm::utility::zero<ValueType>() && !newSccAsBitVector.get(predecessor.getColumn())) {
                                entryStates.set(state);
                            }
                        }
                    }
                    
                    // Recursively descend in SCC-hierarchy.
                    uint_fast64_t depth = treatScc(matrix, oneStepProbabilities, entryStates, newSccAsBitVector, forwardTransitions, backwardTransitions, !storm::settings::sparseDtmcEliminationModelCheckerSettings().isEliminateEntryStatesLastSet(), level + 1, maximalSccSize, entryStateQueue, stateRewards, statePriorities);
                    maximalDepth = std::max(maximalDepth, depth);
                }
                
            } else {
                // In this case, we perform simple state elimination in the current SCC.
                STORM_LOG_TRACE("SCC of size " << scc.getNumberOfSetBits() << " is small enough to be eliminated directly.");
                storm::storage::BitVector remainingStates = scc & ~entryStates;
                
                std::vector<uint_fast64_t> states(remainingStates.begin(), remainingStates.end());
                
                // If we are given priorities, sort the trivial SCCs accordingly.
                if (statePriorities) {
                    std::sort(states.begin(), states.end(), [&statePriorities] (storm::storage::sparse::state_type const& a, storm::storage::sparse::state_type const& b) { return statePriorities.get()[a] < statePriorities.get()[b]; });
                }
                
                // Eliminate the remaining states that do not have a self-loop (in the current, i.e. modified)
                // transition probability matrix.
                for (auto const& state : states) {
                    eliminateState(matrix, oneStepProbabilities, state, backwardTransitions, stateRewards);
                }
                
                STORM_LOG_TRACE("Eliminated all states of SCC.");
            }
            
            // Finally, eliminate the entry states (if we are required to do so).
            if (eliminateEntryStates) {
                STORM_LOG_TRACE("Finally, eliminating/adding entry states.");
                for (auto state : entryStates) {
                    eliminateState(matrix, oneStepProbabilities, state, backwardTransitions, stateRewards);
                }
                STORM_LOG_TRACE("Eliminated/added entry states.");
            } else {
                for (auto state : entryStates) {
                    entryStateQueue.push_back(state);
                }
            }
            
            return maximalDepth;
        }
        
        namespace {
            static int chunkCounter = 0;
            static int counter = 0;
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::eliminateState(FlexibleSparseMatrix& matrix, std::vector<ValueType>& oneStepProbabilities, uint_fast64_t state, FlexibleSparseMatrix& backwardTransitions, boost::optional<std::vector<ValueType>>& stateRewards, bool removeForwardTransitions, bool constrained, storm::storage::BitVector const& predecessorConstraint) {
            auto eliminationStart = std::chrono::high_resolution_clock::now();
            
            ++counter;
            STORM_LOG_TRACE("Eliminating state " << state << ".");
            if (counter > matrix.getNumberOfRows() / 10) {
                ++chunkCounter;
                STORM_LOG_INFO("Eliminated " << (chunkCounter * 10) << "% of the states." << std::endl);
                counter = 0;
            }
            
            bool hasSelfLoop = false;
            ValueType loopProbability = storm::utility::zero<ValueType>();
            
            // Start by finding loop probability.
            typename FlexibleSparseMatrix::row_type& currentStateSuccessors = matrix.getRow(state);
            for (auto entryIt = currentStateSuccessors.begin(), entryIte = currentStateSuccessors.end(); entryIt != entryIte; ++entryIt) {
                if (entryIt->getColumn() >= state) {
                    if (entryIt->getColumn() == state) {
                        loopProbability = entryIt->getValue();
                        hasSelfLoop = true;
                        
                        // If we do not clear the forward transitions completely, we need to remove the self-loop,
                        // because we scale all the other outgoing transitions with it anyway..
                        if (!removeForwardTransitions) {
                            currentStateSuccessors.erase(entryIt);
                        }
                    }
                    break;
                }
            }
            
            // Scale all entries in this row with (1 / (1 - loopProbability)) only in case there was a self-loop.
            std::size_t scaledSuccessors = 0;
            if (hasSelfLoop) {
                STORM_LOG_ASSERT(!comparator.isOne(loopProbability), "Must not eliminate state with probability 1 self-loop.");
                loopProbability = storm::utility::one<ValueType>() / (storm::utility::one<ValueType>() - loopProbability);
                storm::utility::simplify(loopProbability);
                for (auto& entry : matrix.getRow(state)) {
                    // Only scale the non-diagonal entries.
                    if (entry.getColumn() != state) {
                        ++scaledSuccessors;
                        entry.setValue(storm::utility::simplify(entry.getValue() * loopProbability));
                    }
                }
                if (!stateRewards) {
                    oneStepProbabilities[state] = oneStepProbabilities[state] * loopProbability;
                }
            }
            
            STORM_LOG_TRACE((hasSelfLoop ? "State has self-loop." : "State does not have a self-loop."));
            
            // Now connect the predecessors of the state being eliminated with its successors.
            typename FlexibleSparseMatrix::row_type& currentStatePredecessors = backwardTransitions.getRow(state);
            std::size_t numberOfPredecessors = currentStatePredecessors.size();
            std::size_t predecessorForwardTransitionCount = 0;
            
            // In case we have a constrained elimination, we need to keep track of the new predecessors.
            typename FlexibleSparseMatrix::row_type newCurrentStatePredecessors;
            
            // Now go through the predecessors and eliminate the ones (satisfying the constraint if given).
            for (auto const& predecessorEntry : currentStatePredecessors) {
                uint_fast64_t predecessor = predecessorEntry.getColumn();
                
                // Skip the state itself as one of its predecessors.
                if (predecessor == state) {
                    assert(hasSelfLoop);
                    continue;
                }
                
                // Skip the state if the elimination is constrained, but the predecessor is not in the constraint.
                if (constrained && !predecessorConstraint.get(predecessor)) {
                    newCurrentStatePredecessors.emplace_back(predecessor, storm::utility::one<ValueType>());
                    STORM_LOG_TRACE("Not eliminating predecessor " << predecessor << ", because it does not fit the filter.");
                    continue;
                }
                STORM_LOG_TRACE("Eliminating predecessor " << predecessor << ".");
                
                // First, find the probability with which the predecessor can move to the current state, because
                // the other probabilities need to be scaled with this factor.
                typename FlexibleSparseMatrix::row_type& predecessorForwardTransitions = matrix.getRow(predecessor);
                predecessorForwardTransitionCount += predecessorForwardTransitions.size();
                typename FlexibleSparseMatrix::row_type::iterator multiplyElement = std::find_if(predecessorForwardTransitions.begin(), predecessorForwardTransitions.end(), [&](storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type> const& a) { return a.getColumn() == state; });
                
                // Make sure we have found the probability and set it to zero.
                STORM_LOG_THROW(multiplyElement != predecessorForwardTransitions.end(), storm::exceptions::InvalidStateException, "No probability for successor found.");
                ValueType multiplyFactor = multiplyElement->getValue();
                multiplyElement->setValue(storm::utility::zero<ValueType>());
                
                // At this point, we need to update the (forward) transitions of the predecessor.
                typename FlexibleSparseMatrix::row_type::iterator first1 = predecessorForwardTransitions.begin();
                typename FlexibleSparseMatrix::row_type::iterator last1 = predecessorForwardTransitions.end();
                typename FlexibleSparseMatrix::row_type::iterator first2 = currentStateSuccessors.begin();
                typename FlexibleSparseMatrix::row_type::iterator last2 = currentStateSuccessors.end();
                
                typename FlexibleSparseMatrix::row_type newSuccessors;
                newSuccessors.reserve((last1 - first1) + (last2 - first2));
                std::insert_iterator<typename FlexibleSparseMatrix::row_type> result(newSuccessors, newSuccessors.end());
                
                // Now we merge the two successor lists. (Code taken from std::set_union and modified to suit our needs).
                for (; first1 != last1; ++result) {
                    // Skip the transitions to the state that is currently being eliminated.
                    if (first1->getColumn() == state || (first2 != last2 && first2->getColumn() == state)) {
                        if (first1->getColumn() == state) {
                            ++first1;
                        }
                        if (first2 != last2 && first2->getColumn() == state) {
                            ++first2;
                        }
                        continue;
                    }
                    
                    if (first2 == last2) {
                        std::copy_if(first1, last1, result, [&] (storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type> const& a) { return a.getColumn() != state; } );
                        break;
                    }
                    if (first2->getColumn() < first1->getColumn()) {
                        *result = storm::utility::simplify(std::move(*first2 * multiplyFactor));
                        ++first2;
                    } else if (first1->getColumn() < first2->getColumn()) {
                        *result = *first1;
                        ++first1;
                    } else {
                        *result = storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type>(first1->getColumn(), storm::utility::simplify(first1->getValue() + storm::utility::simplify(multiplyFactor * first2->getValue())));
                        ++first1;
                        ++first2;
                    }
                }
                for (; first2 != last2; ++first2) {
                    if (first2->getColumn() != state) {
                        *result = storm::utility::simplify(std::move(*first2 * multiplyFactor));
                    }
                }
                
                // Now move the new transitions in place.
                predecessorForwardTransitions = std::move(newSuccessors);
                
                if (!stateRewards) {
                    // Add the probabilities to go to a target state in just one step if we have to compute probabilities.
                    oneStepProbabilities[predecessor] += storm::utility::simplify(multiplyFactor * oneStepProbabilities[state]);
                    STORM_LOG_TRACE("Fixed new next-state probabilities of predecessor states.");
                } else {
                    // If we are computing rewards, we basically scale the state reward of the state to eliminate and
                    // add the result to the state reward of the predecessor.
                    if (hasSelfLoop) {
                        stateRewards.get()[predecessor] += storm::utility::simplify(multiplyFactor * loopProbability * stateRewards.get()[state]);
                    } else {
                        stateRewards.get()[predecessor] += storm::utility::simplify(multiplyFactor * stateRewards.get()[state]);
                    }
                }
            }
            
            // Finally, we need to add the predecessor to the set of predecessors of every successor.
            for (auto const& successorEntry : currentStateSuccessors) {
                typename FlexibleSparseMatrix::row_type& successorBackwardTransitions = backwardTransitions.getRow(successorEntry.getColumn());
                
                // Delete the current state as a predecessor of the successor state only if we are going to remove the
                // current state's forward transitions.
                if (removeForwardTransitions) {
                    typename FlexibleSparseMatrix::row_type::iterator elimIt = std::find_if(successorBackwardTransitions.begin(), successorBackwardTransitions.end(), [&](storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type> const& a) { return a.getColumn() == state; });
                    STORM_LOG_ASSERT(elimIt != successorBackwardTransitions.end(), "Expected a proper backward transition, but found none.");
                    successorBackwardTransitions.erase(elimIt);
                }
                
                typename FlexibleSparseMatrix::row_type::iterator first1 = successorBackwardTransitions.begin();
                typename FlexibleSparseMatrix::row_type::iterator last1 = successorBackwardTransitions.end();
                typename FlexibleSparseMatrix::row_type::iterator first2 = currentStatePredecessors.begin();
                typename FlexibleSparseMatrix::row_type::iterator last2 = currentStatePredecessors.end();
                
                typename FlexibleSparseMatrix::row_type newPredecessors;
                newPredecessors.reserve((last1 - first1) + (last2 - first2));
                std::insert_iterator<typename FlexibleSparseMatrix::row_type> result(newPredecessors, newPredecessors.end());
                
                if (!constrained) {
                    for (; first1 != last1; ++result) {
                        if (first2 == last2) {
                            std::copy(first1, last1, result);
                            break;
                        }
                        if (first2->getColumn() < first1->getColumn()) {
                            if (first2->getColumn() != state) {
                                *result = *first2;
                            }
                            ++first2;
                        } else {
                            *result = *first1;
                            if (first1->getColumn() == first2->getColumn()) {
                                ++first2;
                            }
                            ++first1;
                        }
                    }
                    std::copy_if(first2, last2, result, [&] (storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type> const& a) { return a.getColumn() != state; });
                } else {
                    // If the elimination is constrained, we need to be more selective when we set the new predecessors
                    // of the successor state.
                    for (; first1 != last1; ++result) {
                        if (first2 == last2) {
                            std::copy(first1, last1, result);
                            break;
                        }
                        if (first2->getColumn() < first1->getColumn()) {
                            if (first2->getColumn() != state) {
                                *result = *first2;
                            }
                            ++first2;
                        } else {
                            *result = *first1;
                            if (first1->getColumn() == first2->getColumn()) {
                                ++first2;
                            }
                            ++first1;
                        }
                    }
                    std::copy_if(first2, last2, result, [&] (storm::storage::MatrixEntry<typename FlexibleSparseMatrix::index_type, typename FlexibleSparseMatrix::value_type> const& a) { return a.getColumn() != state && (!constrained || predecessorConstraint.get(a.getColumn())); });
                }
                
                // Now move the new predecessors in place.
                successorBackwardTransitions = std::move(newPredecessors);
            }
            STORM_LOG_TRACE("Fixed predecessor lists of successor states.");
            
            if (removeForwardTransitions) {
                // Clear the eliminated row to reduce memory consumption.
                currentStateSuccessors.clear();
                currentStateSuccessors.shrink_to_fit();
            }
            if (!constrained) {
                currentStatePredecessors.clear();
                currentStatePredecessors.shrink_to_fit();
            } else {
                currentStatePredecessors = std::move(newCurrentStatePredecessors);
            }
            
            auto eliminationEnd = std::chrono::high_resolution_clock::now();
            auto eliminationTime = eliminationEnd - eliminationStart;
        }
        
        template<typename ValueType>
        SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::FlexibleSparseMatrix(index_type rows) : data(rows) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::reserveInRow(index_type row, index_type numberOfElements) {
            this->data[row].reserve(numberOfElements);
        }
        
        template<typename ValueType>
        typename SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::row_type& SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::getRow(index_type index) {
            return this->data[index];
        }
        
        template<typename ValueType>
        typename SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::row_type const& SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::getRow(index_type index) const {
            return this->data[index];
        }
        
        template<typename ValueType>
        typename SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::index_type SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::getNumberOfRows() const {
            return this->data.size();
        }
        
        template<typename ValueType>
        bool SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::hasSelfLoop(storm::storage::sparse::state_type state) const {
            for (auto const& entry : this->getRow(state)) {
                if (entry.getColumn() < state) {
                    continue;
                } else if (entry.getColumn() > state) {
                    return false;
                } else if (entry.getColumn() == state) {
                    return true;
                }
            }
            return false;
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::print() const {
            for (uint_fast64_t index = 0; index < this->data.size(); ++index) {
                std::cout << index << " - ";
                for (auto const& element : this->getRow(index)) {
                    std::cout << "(" << element.getColumn() << ", " << element.getValue() << ") ";
                }
                std::cout << std::endl;
            }
        }
        
#ifdef STORM_HAVE_CARL
        template<>
        storm::storage::SparseMatrix<double> SparseDtmcEliminationModelChecker<storm::RationalFunction>::FlexibleSparseMatrix::instantiateAsDouble(std::map<storm::Variable, storm::RationalFunction::CoeffType> const& substitutions, storm::storage::BitVector const& filter, bool addSinkState, std::vector<storm::RationalFunction> const& oneStepProbabilities, bool addSelfLoops) const {
            
            //Check if the arguments are as expected
            STORM_LOG_THROW((std::is_same<storm::RationalFunction::CoeffType, cln::cl_RA>::value), storm::exceptions::IllegalArgumentException, "Unexpected Type of Coefficients");
            STORM_LOG_THROW(filter.size()==this->getNumberOfRows(), storm::exceptions::IllegalArgumentException, "Unexpected size of the filter");
            STORM_LOG_THROW(oneStepProbabilities.empty() || oneStepProbabilities.size()==this->getNumberOfRows(), storm::exceptions::IllegalArgumentException, "Unexpected size of the oneStepProbabilities");
            
            //get data for a Matrix builder as well as a mapping from old state indices to the new ones
            index_type numTransitions=0;
            std::vector<storm::storage::sparse::state_type> newStateIndexMap(this->getNumberOfRows(), this->getNumberOfRows()); //initialize with some illegal index to easily check if a transition leads to an unselected state
            storm::storage::sparse::state_type newStateIndex=0;
            for(auto const& state : filter){
                numTransitions += this->getRow(state).size();
                if(addSelfLoops && !hasSelfLoop(state)){
                    ++numTransitions;
                }
                if(!oneStepProbabilities.empty() && !oneStepProbabilities[state].isZero()){
                    ++numTransitions;
                }
                if(addSinkState){
                    ++numTransitions; //we always add a transition here.. Todo: consider other ways with less memory consumption to handle this
                }
                newStateIndexMap[state]=newStateIndex;
                ++newStateIndex;
            }
            index_type numStates=filter.getNumberOfSetBits();
            STORM_LOG_ASSERT(newStateIndex==numStates, "unexpected number of new states");
            storm::storage::sparse::state_type targetState =0;
            storm::storage::sparse::state_type sinkState=0;
            if(!oneStepProbabilities.empty()){
                targetState=numStates;
                ++numStates;
                ++numTransitions;
            }
            if(addSinkState){
                sinkState=numStates;
                ++numStates;
                ++numTransitions;
            }
            storm::storage::SparseMatrixBuilder<double> matrixBuilder(numStates, numStates, numTransitions);
            //fill in the data row by row            
            for(auto const& oldStateIndex : filter){
                double missingProbability = 1.0;
                if(this->getRow(oldStateIndex).empty()){
                    if(addSelfLoops){
                        matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], newStateIndexMap[oldStateIndex], 0.0);
                    }
                }
                else{
                    const_iterator entry = this->getRow(oldStateIndex).begin();
                    for(; entry<this->getRow(oldStateIndex).end() && entry->getColumn()<oldStateIndex; ++entry){
                        double value = cln::double_approx(entry->getValue().evaluate(substitutions));
                        missingProbability-=value;
                        storm::storage::sparse::state_type column = newStateIndexMap[entry->getColumn()];
                        STORM_LOG_THROW(column<numStates, storm::exceptions::IllegalArgumentException, "Illegal filter: Selected a state that has a transition to an unselected state.");
                        matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], column, value);
                    }
                    if(addSelfLoops && entry->getColumn()!=oldStateIndex){
                        matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], newStateIndexMap[oldStateIndex], 0.0);
                    }
                    for(; entry < this->getRow(oldStateIndex).end(); ++entry){
                        double value = cln::double_approx(entry->getValue().evaluate(substitutions));
                        missingProbability-=value;
                        storm::storage::sparse::state_type column = newStateIndexMap[entry->getColumn()];
                        STORM_LOG_THROW(column<numStates, storm::exceptions::IllegalArgumentException, "Illegal filter: Selected a state that has a transition to an unselected state.");
                        matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], column, value);
                    }
                }
                if(!oneStepProbabilities.empty() && !oneStepProbabilities[oldStateIndex].isZero()){
                    double value = cln::double_approx(oneStepProbabilities[oldStateIndex].evaluate(substitutions));
                    missingProbability-=value;
                    matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], targetState, value);
                }
                if(addSinkState){ // one could also check if the missing probability is not zero, but then the number of transitions is not clear at the beginning...  && !storm::utility::ConstantsComparator<double>.isZero(missingProbability)){
                    STORM_LOG_ASSERT(missingProbability> -storm::settings::generalSettings().getPrecision(), "The missing probability is negative.");
                    matrixBuilder.addNextValue(newStateIndexMap[oldStateIndex], sinkState, missingProbability);
                }
            }
            if (!oneStepProbabilities.empty()){
                matrixBuilder.addNextValue(targetState, targetState, 1.0);
            }
            if (addSinkState){
                matrixBuilder.addNextValue(sinkState, sinkState, 1.0);
            }
       
            return matrixBuilder.build();
        }
        
         template<typename ValueType>
        storm::storage::SparseMatrix<double> SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix::instantiateAsDouble(std::map<storm::Variable, storm::RationalFunction::CoeffType> const& substitutions, storm::storage::BitVector const& filter, bool addSinkState, std::vector<ValueType> const& oneStepProbabilities, bool addSelfLoops) const{            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "Instantiation of flexible matrix is not supported for this type");
        }
#endif
        
        
        template<typename ValueType>
        typename SparseDtmcEliminationModelChecker<ValueType>::FlexibleSparseMatrix SparseDtmcEliminationModelChecker<ValueType>::getFlexibleSparseMatrix(storm::storage::SparseMatrix<ValueType> const& matrix, bool setAllValuesToOne) {
            FlexibleSparseMatrix flexibleMatrix(matrix.getRowCount());
            
            // A comparator used for comparing probabilities.
            storm::utility::ConstantsComparator<ValueType> comparator;
            
            for (typename FlexibleSparseMatrix::index_type rowIndex = 0; rowIndex < matrix.getRowCount(); ++rowIndex) {
                typename storm::storage::SparseMatrix<ValueType>::const_rows row = matrix.getRow(rowIndex);
                flexibleMatrix.reserveInRow(rowIndex, row.getNumberOfEntries());
                
                for (auto const& element : row) {
                    // If the probability is zero, we skip this entry.
                    if (comparator.isZero(element.getValue())) {
                        continue;
                    }
                    
                    if (setAllValuesToOne) {
                        flexibleMatrix.getRow(rowIndex).emplace_back(element.getColumn(), storm::utility::one<ValueType>());
                    } else {
                        flexibleMatrix.getRow(rowIndex).emplace_back(element);
                    }
                }
            }
            
            return flexibleMatrix;
        }
#ifdef STORM_HAVE_CARL
                
        template<>
        void SparseDtmcEliminationModelChecker<storm::RationalFunction>::eliminateStates(storm::storage::BitVector& subsystem, FlexibleSparseMatrix& flexibleMatrix, std::vector<storm::RationalFunction>& oneStepProbabilities, FlexibleSparseMatrix& flexibleBackwardTransitions, storm::storage::BitVector const& initialstates){
            //.. Todo: chose between different strategies for elimination of states
            
            //only eliminate states with constant outgoing transitions and non-initial states.
            /* this does not work since the transitions change while processing
            storm::storage::BitVector constantOutgoing(subsystem.size(), true);
            storm::storage::BitVector constantIncoming(subsystem.size(), true);
            for(FlexibleSparseMatrix::index_type row=0; row<flexibleMatrix.getNumberOfRows(); ++row){
                for(auto const& entry : flexibleMatrix.getRow(row)){
                    std::cout << "en " << entry.getValue() << std::endl;
                    if(!entry.getValue().isConstant()){
                        std::cout << "    its not const" << std::endl;
                        constantOutgoing.set(row,false);
                        constantIncoming.set(entry.getColumn(), false);
                    }
                }
            }*/
            storm::storage::BitVector statesToEliminate = ~initialstates;
            std::cout << "can eliminate " << statesToEliminate.getNumberOfSetBits() << " of " << statesToEliminate.size() << "states." << std::endl;
            
            std::vector<storm::storage::sparse::state_type> states(statesToEliminate.begin(), statesToEliminate.end());
            //todo some special ordering?
            STORM_LOG_DEBUG("Eliminating " << states.size() << " states." << std::endl);
            boost::optional<std::vector<storm::RationalFunction>> missingStateRewards;
            for (auto const& state : states) {
                bool onlyConstantOutgoingTransitions=true;
                for(auto const& entry : flexibleMatrix.getRow(state)){
                    if(!entry.getValue().isConstant()){
                        onlyConstantOutgoingTransitions=false;
                        break;
                    }
                }
                if(onlyConstantOutgoingTransitions){
                    eliminateState(flexibleMatrix, oneStepProbabilities, state, flexibleBackwardTransitions, missingStateRewards);
                    subsystem.set(state,false);
                }
            }
            STORM_LOG_DEBUG("Eliminated " << states.size() << " states." << std::endl);
            //Note: we could also "eliminate" the initial state to get rid of its selfloop
            //*/
            
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::eliminateStates(storm::storage::BitVector& subsystem, FlexibleSparseMatrix& flexibleMatrix, std::vector<ValueType>& oneStepProbabilities, FlexibleSparseMatrix& flexibleBackwardTransitions, storm::storage::BitVector const& initialstates){
            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "elimination of states not suported for this type");
        }

        
        template<>
        void SparseDtmcEliminationModelChecker<storm::RationalFunction>::formulateModelWithSMT(storm::solver::Smt2SmtSolver& solver, std::vector<storm::RationalFunction::PolyType>& stateProbVars, storm::storage::BitVector const& subsystem, FlexibleSparseMatrix const& flexibleMatrix, std::vector<storm::RationalFunction> const& oneStepProbabilities){
            carl::VariablePool& varPool = carl::VariablePool::getInstance();
            
            //first add a state variable for every state in the subsystem, providing that such a variable does not already exist.
            for (storm::storage::sparse::state_type state : subsystem){
                if(stateProbVars[state].isZero()){ //variable does not exist yet
                    storm::Variable stateVar = varPool.getFreshVariable("p_" + std::to_string(state));
                    std::shared_ptr<carl::Cache<carl::PolynomialFactorizationPair<storm::RawPolynomial>>> cache(new carl::Cache<carl::PolynomialFactorizationPair<storm::RawPolynomial>>());
                    storm::RationalFunction::PolyType stateVarAsPoly(storm::RationalFunction::PolyType::PolyType(stateVar), cache);

                    //each variable is in the interval [0,1]
                    solver.add(storm::RationalFunction(stateVarAsPoly), storm::CompareRelation::GEQ, storm::RationalFunction(0));
                    solver.add(storm::RationalFunction(stateVarAsPoly), storm::CompareRelation::LEQ, storm::RationalFunction(1));
                    stateProbVars[state] = stateVarAsPoly;
                }
            }
            
            //now lets add the actual transitions
            for (storm::storage::sparse::state_type state : subsystem){
                storm::RationalFunction reachProbability(oneStepProbabilities[state]);
                for(auto const& transition : flexibleMatrix.getRow(state)){
                    reachProbability += transition.getValue() * stateProbVars[transition.getColumn()];
                }
                //Todo: depending on the objective (i.e. the formlua) it suffices to use LEQ or GEQ here... maybe this is faster?
                solver.add(storm::RationalFunction(stateProbVars[state]), storm::CompareRelation::EQ, reachProbability);
            }
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::formulateModelWithSMT(storm::solver::Smt2SmtSolver& solver, std::vector<storm::RationalFunction::PolyType>& stateProbVars, storm::storage::BitVector const& subsystem, FlexibleSparseMatrix const& flexibleMatrix, std::vector<storm::RationalFunction> const& oneStepProbabilities){
            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "SMT formulation is not supported for this type");
        }
        
        template<>
        void SparseDtmcEliminationModelChecker<storm::RationalFunction>::restrictProbabilityVariables(storm::solver::Smt2SmtSolver& solver, std::vector<storm::RationalFunction::PolyType> const& stateProbVars, storm::storage::BitVector const& subsystem, FlexibleSparseMatrix const& flexibleMatrix, std::vector<storm::RationalFunction> const& oneStepProbabilities, std::vector<ParameterRegion> const& regions, storm::logic::ComparisonType const& compType){
            //We are going to build a new (non parametric) DTMC 
            STORM_LOG_WARN("the probability restriction is not really correct, it only helps if there is a 'sat' answer");
            storm::storage::sparse::state_type const numOfStates=subsystem.getNumberOfSetBits() + 2; //subsystem + target state + sink state
            storm::models::sparse::StateLabeling stateLabeling(numOfStates);
            stateLabeling.addLabel("init", storm::storage::BitVector(numOfStates, true));
            storm::storage::BitVector targetLabel(numOfStates, false);
            targetLabel.set(numOfStates-2, true);
            stateLabeling.addLabel("target", std::move(targetLabel));
            storm::storage::BitVector sinkLabel(numOfStates, false);
            sinkLabel.set(numOfStates-1, true);
            stateLabeling.addLabel("sink", std::move(sinkLabel));
            std::map<storm::Variable, storm::RationalFunction::CoeffType> substitutions;
            for(auto const& parRegion : regions){
                substitutions.insert(std::pair<storm::Variable,storm::RationalFunction::CoeffType>(parRegion.variable, parRegion.upperBound)); //todo: (upper+lower)/2 ?
            }
            storm::models::sparse::Dtmc<double> dtmc(flexibleMatrix.instantiateAsDouble(substitutions, subsystem, true, oneStepProbabilities, true), std::move(stateLabeling));
            
            //perform model checking on this dtmc
            storm::modelchecker::SparseDtmcPrctlModelChecker<double> modelChecker(dtmc);
            std::shared_ptr<storm::logic::Formula> targetFormulaPtr(new storm::logic::AtomicLabelFormula("target"));
            storm::logic::EventuallyFormula eventuallyFormula(targetFormulaPtr);
            std::unique_ptr<CheckResult> resultPtr = modelChecker.computeEventuallyProbabilities(eventuallyFormula);
            std::vector<double> resultVector = resultPtr->asExplicitQuantitativeCheckResult<double>().getValueVector();
            
            //formulate constraints for the solver
            storm::CompareRelation boundRelation;
            switch (compType){
                case storm::logic::ComparisonType::Greater:
                    boundRelation=storm::CompareRelation::LEQ;
                    break;
                case storm::logic::ComparisonType::GreaterEqual:
                    boundRelation=storm::CompareRelation::LEQ;
                    break;
                case storm::logic::ComparisonType::Less:
                    boundRelation=storm::CompareRelation::GEQ;
                    break;
                case storm::logic::ComparisonType::LessEqual:
                    boundRelation=storm::CompareRelation::GEQ;
                    break;
                default:
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "the comparison relation of the formula is not supported");
            }
            uint_fast64_t boundDenominator = 1.0/storm::settings::generalSettings().getPrecision(); //we need to approx. the obtained bounds as rational numbers
            storm::storage::sparse::state_type subsystemState=0; //the subsystem uses other state indices
            for(storm::storage::sparse::state_type state : subsystem){
                uint_fast64_t boundNumerator = resultVector[subsystemState]*boundDenominator;
                storm::RationalFunction bound(boundNumerator);
                bound = bound/boundDenominator;
                //Todo: non-exact values might be problematic here...
                solver.add(storm::RationalFunction(stateProbVars[state]), boundRelation, bound);
                ++subsystemState;
            }
        }
        
        template<typename ValueType>
        void SparseDtmcEliminationModelChecker<ValueType>::restrictProbabilityVariables(storm::solver::Smt2SmtSolver& solver, std::vector<storm::RationalFunction::PolyType> const& stateProbVars, storm::storage::BitVector const& subsystem, FlexibleSparseMatrix const& flexibleMatrix, std::vector<storm::RationalFunction> const& oneStepProbabilities, std::vector<ParameterRegion> const& regions, storm::logic::ComparisonType const& compType){
            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "restricting Probability Variables is not supported for this type");
        }
        
        template<>
        bool SparseDtmcEliminationModelChecker<storm::RationalFunction>::checkRegion(storm::logic::Formula const& formula, std::vector<SparseDtmcEliminationModelChecker<storm::RationalFunction>::ParameterRegion> parameterRegions){
            //Note: this is an 'experimental' implementation
            
            std::chrono::high_resolution_clock::time_point timeStart = std::chrono::high_resolution_clock::now();
                
            //Start with some preprocessing (inspired by computeUntilProbabilities...)
            //for simplicity we only support state formulas with eventually (e.g. P<0.5 [ F "target" ])
            //get the (sub)formulae and the vector of target states
            STORM_LOG_THROW(formula.isStateFormula(), storm::exceptions::IllegalArgumentException, "expected a stateFormula");
            STORM_LOG_THROW(formula.asStateFormula().isProbabilityOperatorFormula(), storm::exceptions::IllegalArgumentException, "expected a probabilityOperatorFormula");
            storm::logic::ProbabilityOperatorFormula const& probOpForm=formula.asStateFormula().asProbabilityOperatorFormula();
            STORM_LOG_THROW(probOpForm.hasBound(), storm::exceptions::IllegalArgumentException, "The formula has no bound");
            STORM_LOG_THROW(probOpForm.getSubformula().asPathFormula().isEventuallyFormula(), storm::exceptions::IllegalArgumentException, "expected an eventually subformula");
            storm::logic::EventuallyFormula const& eventuallyFormula = probOpForm.getSubformula().asPathFormula().asEventuallyFormula();
            std::unique_ptr<CheckResult> targetStatesResultPtr = this->check(eventuallyFormula.getSubformula());
            storm::storage::BitVector const& targetStates = targetStatesResultPtr->asExplicitQualitativeCheckResult().getTruthValuesVector();
            // Do some sanity checks to establish some required properties.
            STORM_LOG_THROW(model.getInitialStates().getNumberOfSetBits() == 1, storm::exceptions::IllegalArgumentException, "Input model is required to have exactly one initial state.");
            storm::storage::sparse::state_type initialState = *model.getInitialStates().begin();
            // Then, compute the subset of states that has a probability of 0 or 1, respectively.
            std::pair<storm::storage::BitVector, storm::storage::BitVector> statesWithProbability01 = storm::utility::graph::performProb01(model, storm::storage::BitVector(model.getNumberOfStates(),true), targetStates);
            storm::storage::BitVector statesWithProbability0 = statesWithProbability01.first;
            storm::storage::BitVector statesWithProbability1 = statesWithProbability01.second;
            storm::storage::BitVector maybeStates = ~(statesWithProbability0 | statesWithProbability1);
            // If the initial state is known to have either probability 0 or 1, we can directly return the result.
            if (model.getInitialStates().isDisjointFrom(maybeStates)) {
                STORM_LOG_DEBUG("The probability of all initial states was found in a preprocessing step.");
                double res= statesWithProbability0.get(*model.getInitialStates().begin()) ? 0.0 : 1.0;
                switch (probOpForm.getComparisonType()){
                    case storm::logic::ComparisonType::Greater:
                        return (res > probOpForm.getBound());
                    case storm::logic::ComparisonType::GreaterEqual:
                        return (res >= probOpForm.getBound());
                    case storm::logic::ComparisonType::Less:
                        return (res < probOpForm.getBound());
                    case storm::logic::ComparisonType::LessEqual:
                        return (res <= probOpForm.getBound());
                    default:
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "the comparison relation of the formula is not supported");
                }
            }
            // Determine the set of states that is reachable from the initial state without jumping over a target state.
            storm::storage::BitVector reachableStates = storm::utility::graph::getReachableStates(model.getTransitionMatrix(), model.getInitialStates(), maybeStates, statesWithProbability1);
            // Subtract from the maybe states the set of states that is not reachable (on a path from the initial to a target state).
            maybeStates &= reachableStates;
            // Create a vector for the probabilities to go to a state with probability 1 in one step.
            std::vector<storm::RationalFunction> oneStepProbabilities = model.getTransitionMatrix().getConstrainedRowSumVector(maybeStates, statesWithProbability1);
            // Determine the set of initial states of the sub-model.
            storm::storage::BitVector newInitialStates = model.getInitialStates() % maybeStates;
            // We then build the submatrix that only has the transitions of the maybe states.
            storm::storage::SparseMatrix<storm::RationalFunction> submatrix = model.getTransitionMatrix().getSubmatrix(false, maybeStates, maybeStates);
            storm::storage::SparseMatrix<storm::RationalFunction> submatrixTransposed = submatrix.transpose();
            // Then, we convert the reduced matrix to a more flexible format to be able to perform state elimination more easily.
            FlexibleSparseMatrix flexibleMatrix = getFlexibleSparseMatrix(submatrix);
            FlexibleSparseMatrix flexibleBackwardTransitions = getFlexibleSparseMatrix(submatrixTransposed, true);
            
            std::chrono::high_resolution_clock::time_point timePreprocessingEnd = std::chrono::high_resolution_clock::now();
            
           // Create a bit vector that represents the current subsystem, i.e., states that we have not eliminated.
            storm::storage::BitVector subsystem = storm::storage::BitVector(submatrix.getRowCount(), true);
            eliminateStates(subsystem, flexibleMatrix, oneStepProbabilities, flexibleBackwardTransitions, newInitialStates);
            
            std::chrono::high_resolution_clock::time_point timeStateElemEnd = std::chrono::high_resolution_clock::now();
            
            // SMT formulation of resulting pdtmc
            storm::expressions::ExpressionManager manager; //this manager will do nothing as we will use carl expressions
            storm::solver::Smt2SmtSolver solver(manager, true);
            // we will introduce a variable for every state which encodes the probability to reach a target state from this state.
            // we will store them as polynomials to easily use operations with rational functions
            std::vector<storm::RationalFunction::PolyType> stateProbVars(subsystem.size(), storm::RationalFunction::PolyType(0));
            // todo maybe introduce the parameters already at this point?
            formulateModelWithSMT(solver, stateProbVars, subsystem, flexibleMatrix, oneStepProbabilities);
            
            //the property should be satisfied in the initial state for all parameters.
            //this is equivalent to:
            //the negation of the property should not be satisfied for some parameter valuation.
            //Hence, we flip the comparison relation and later check whether all the constraints are unsat.
            storm::CompareRelation propertyCompRel;
            switch (probOpForm.getComparisonType()){
                case storm::logic::ComparisonType::Greater:
                    propertyCompRel=storm::CompareRelation::LEQ;
                    break;
                case storm::logic::ComparisonType::GreaterEqual:
                    propertyCompRel=storm::CompareRelation::LT;
                    break;
                case storm::logic::ComparisonType::Less:
                    propertyCompRel=storm::CompareRelation::GEQ;
                    break;
                case storm::logic::ComparisonType::LessEqual:
                    propertyCompRel=storm::CompareRelation::GT;
                    break;
                default:
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "the comparison relation of the formula is not supported");
            }
            uint_fast64_t thresholdDenominator = 1.0/storm::settings::generalSettings().getPrecision();
            uint_fast64_t thresholdNumerator = probOpForm.getBound()*thresholdDenominator;
            storm::RationalFunction threshold(thresholdNumerator);
            threshold = threshold / thresholdDenominator;
            solver.add(storm::RationalFunction(stateProbVars[*newInitialStates.begin()]), propertyCompRel, threshold);
            
            //the bounds for the parameters
            solver.push();
            for(auto param : parameterRegions){
                storm::RawPolynomial lB(param.variable);
                lB -= param.lowerBound;
                solver.add(carl::Constraint<storm::RawPolynomial>(lB,storm::CompareRelation::GEQ));
                storm::RawPolynomial uB(param.variable);
                uB -= param.upperBound;
                solver.add(carl::Constraint<storm::RawPolynomial>(uB,storm::CompareRelation::LEQ));
            }
            
            std::chrono::high_resolution_clock::time_point timeSmtFormulationEnd = std::chrono::high_resolution_clock::now();
            
            // find further restriction on probabilities
            //restrictProbabilityVariables(solver,stateProbVars,subsystem,flexibleMatrix,oneStepProbabilities, parameterRegions, probOpForm.getComparisonType());
            
            std::chrono::high_resolution_clock::time_point timeRestrictingEnd = std::chrono::high_resolution_clock::now();
            
            std::cout << "start solving ..." << std::endl;
            bool result;
                switch (solver.check()){
                case storm::solver::SmtSolver::CheckResult::Sat:
                    std::cout << "sat!" << std::endl;
                    result=false;
                    break;
                case storm::solver::SmtSolver::CheckResult::Unsat:
                    std::cout << "unsat!" << std::endl;
                    result=true;
                    break;
                case storm::solver::SmtSolver::CheckResult::Unknown:
                    std::cout << "unknown!" << std::endl;
                    STORM_LOG_THROW(false, storm::exceptions::UnexpectedException, "Could not solve the SMT-Problem (Check-result: Unknown)")
                    result=false;
                    break;
                default:
                    STORM_LOG_THROW(false, storm::exceptions::UnexpectedException, "Could not solve the SMT-Problem (Check-result: Unknown)")
                    result=false;
            }
            
            std::chrono::high_resolution_clock::time_point timeSolvingEnd = std::chrono::high_resolution_clock::now();    
                
            std::chrono::high_resolution_clock::duration timePreprocessing = timePreprocessingEnd - timeStart;
            std::chrono::high_resolution_clock::duration timeStateElem = timeStateElemEnd - timePreprocessingEnd;
            std::chrono::high_resolution_clock::duration timeSmtFormulation = timeSmtFormulationEnd - timeStateElemEnd;
            std::chrono::high_resolution_clock::duration timeRestricting = timeRestrictingEnd - timeSmtFormulationEnd;
            std::chrono::high_resolution_clock::duration timeSolving = timeSolvingEnd- timeRestrictingEnd;
            std::chrono::high_resolution_clock::duration timeOverall = timeSolvingEnd - timeStart;
            std::chrono::milliseconds timePreprocessingInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timePreprocessing);
            std::chrono::milliseconds timeStateElemInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeStateElem);
            std::chrono::milliseconds timeSmtFormulationInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeSmtFormulation);
            std::chrono::milliseconds timeRestrictingInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeRestricting);
            std::chrono::milliseconds timeSolvingInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeSolving);
            std::chrono::milliseconds timeOverallInMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeOverall);
            STORM_PRINT_AND_LOG(std::endl << "required time: " << timeOverallInMilliseconds.count() << "ms. Time Breakdown:" << std::endl);
            STORM_PRINT_AND_LOG("    * " << timePreprocessingInMilliseconds.count() << "ms for Preprocessing" << std::endl);
            STORM_PRINT_AND_LOG("    * " << timeStateElemInMilliseconds.count() << "ms for StateElemination" << std::endl);
            STORM_PRINT_AND_LOG("    * " << timeSmtFormulationInMilliseconds.count() << "ms for SmtFormulation" << std::endl);
            STORM_PRINT_AND_LOG("    * " << timeRestrictingInMilliseconds.count() << "ms for Restricting" << std::endl);
            STORM_PRINT_AND_LOG("    * " << timeSolvingInMilliseconds.count() << "ms for Solving" << std::endl);

            return result;
        }

        template<typename ValueType>
        bool SparseDtmcEliminationModelChecker<ValueType>::checkRegion(storm::logic::Formula const& formula, std::vector<SparseDtmcEliminationModelChecker<ValueType>::ParameterRegion> parameterRegions){
            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "Region check is not supported for this type");
        }
        
#endif        
        template class SparseDtmcEliminationModelChecker<double>;
        
#ifdef STORM_HAVE_CARL
        template class SparseDtmcEliminationModelChecker<storm::RationalFunction>;
#endif
    } // namespace modelchecker
} // namespace storm
