#include "storm/models/sparse/MarkovAutomaton.h"

#include "storm/adapters/RationalFunctionAdapter.h"
#include "storm/models/sparse/StandardRewardModel.h"
#include "storm/solver/stateelimination/StateEliminator.h"
#include "storm/storage/FlexibleSparseMatrix.h"
#include "storm/utility/constants.h"
#include "storm/utility/ConstantsComparator.h"
#include "storm/utility/vector.h"
#include "storm/utility/macros.h"
#include "storm/utility/graph.h"
#include "storm/transformer/SubsystemBuilder.h"

#include "storm/exceptions/InvalidArgumentException.h"

namespace storm {
    namespace models {
        namespace sparse {
            
            template <typename ValueType, typename RewardModelType>
            MarkovAutomaton<ValueType, RewardModelType>::MarkovAutomaton(storm::storage::SparseMatrix<ValueType> const& transitionMatrix,
                                                                         storm::models::sparse::StateLabeling const& stateLabeling,
                                                                         storm::storage::BitVector const& markovianStates,
                                                                         std::unordered_map<std::string, RewardModelType> const& rewardModels)
                    : MarkovAutomaton<ValueType, RewardModelType>(storm::storage::sparse::ModelComponents<ValueType, RewardModelType>(transitionMatrix, stateLabeling, rewardModels, true, markovianStates)) {
                // Intentionally left empty
            }
            
            template <typename ValueType, typename RewardModelType>
            MarkovAutomaton<ValueType, RewardModelType>::MarkovAutomaton(storm::storage::SparseMatrix<ValueType>&& transitionMatrix,
                                                                         storm::models::sparse::StateLabeling&& stateLabeling,
                                                                         storm::storage::BitVector&& markovianStates,
                                                                         std::unordered_map<std::string, RewardModelType>&& rewardModels)
                    : MarkovAutomaton<ValueType, RewardModelType>(storm::storage::sparse::ModelComponents<ValueType, RewardModelType>(std::move(transitionMatrix), std::move(stateLabeling), std::move(rewardModels), true, std::move(markovianStates))) {
                // Intentionally left empty
            }
            
            template <typename ValueType, typename RewardModelType>
            MarkovAutomaton<ValueType, RewardModelType>::MarkovAutomaton(storm::storage::sparse::ModelComponents<ValueType, RewardModelType> const& components) : NondeterministicModel<ValueType, RewardModelType>(ModelType::MarkovAutomaton, components), markovianStates(components.markovianStates.get()) {
                
                if (components.exitRates) {
                    exitRates = components.exitRates.get();
                }
                
                if (components.rateTransitions) {
                    this->turnRatesToProbabilities();
                }
                closed = this->checkIsClosed();
            }
            
            template <typename ValueType, typename RewardModelType>
            MarkovAutomaton<ValueType, RewardModelType>::MarkovAutomaton(storm::storage::sparse::ModelComponents<ValueType, RewardModelType>&& components) : NondeterministicModel<ValueType, RewardModelType>(ModelType::MarkovAutomaton, std::move(components)), markovianStates(std::move(components.markovianStates.get())) {
                
                if (components.exitRates) {
                    exitRates = std::move(components.exitRates.get());
                }

                if (components.rateTransitions) {
                    this->turnRatesToProbabilities();
                }
                closed = this->checkIsClosed();
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::isClosed() const {
                return closed;
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::isHybridState(storm::storage::sparse::state_type state) const {
                return isMarkovianState(state) && (this->getTransitionMatrix().getRowGroupSize(state) > 1);
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::isMarkovianState(storm::storage::sparse::state_type state) const {
                return this->markovianStates.get(state);
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::isProbabilisticState(storm::storage::sparse::state_type state) const {
                return !this->markovianStates.get(state);
            }
            
            template <typename ValueType, typename RewardModelType>
            std::vector<ValueType> const& MarkovAutomaton<ValueType, RewardModelType>::getExitRates() const {
                return this->exitRates;
            }
            
            template <typename ValueType, typename RewardModelType>
            std::vector<ValueType>& MarkovAutomaton<ValueType, RewardModelType>::getExitRates() {
                return this->exitRates;
            }
            
            template <typename ValueType, typename RewardModelType>
            ValueType const& MarkovAutomaton<ValueType, RewardModelType>::getExitRate(storm::storage::sparse::state_type state) const {
                return this->exitRates[state];
            }
            
            template <typename ValueType, typename RewardModelType>
            ValueType MarkovAutomaton<ValueType, RewardModelType>::getMaximalExitRate() const {
                return storm::utility::vector::max_if(this->exitRates, this->markovianStates);
            }
            
            template <typename ValueType, typename RewardModelType>
            storm::storage::BitVector const& MarkovAutomaton<ValueType, RewardModelType>::getMarkovianStates() const {
                return this->markovianStates;
            }
            
            template <typename ValueType, typename RewardModelType>
            void MarkovAutomaton<ValueType, RewardModelType>::close() {
                if (!closed) {
                    // Get the choices that we will keep
                    storm::storage::BitVector keptChoices(this->getNumberOfChoices(), true);
                    for(auto state : this->getMarkovianStates()) {
                        if(this->getTransitionMatrix().getRowGroupSize(state) > 1) {
                            // The state is hybrid, hence, we remove the first choice.
                            keptChoices.set(this->getTransitionMatrix().getRowGroupIndices()[state], false);
                            // Afterwards, the state will no longer be Markovian.
                            this->markovianStates.set(state, false);
                            exitRates[state] = storm::utility::zero<ValueType>();
                        }
                    }
                    
                    if (!keptChoices.full()) {
                        *this = std::move(*storm::transformer::buildSubsystem(*this, storm::storage::BitVector(this->getNumberOfStates(), true), keptChoices, false).model->template as<MarkovAutomaton<ValueType, RewardModelType>>());
                    }
                    
                    // Mark the automaton as closed.
                    closed = true;
                }
            }
            
            template <typename ValueType, typename RewardModelType>
            void MarkovAutomaton<ValueType, RewardModelType>::turnRatesToProbabilities() {
                bool assertRates = (this->exitRates.size() == this->getNumberOfStates());
                if (!assertRates) {
                    STORM_LOG_THROW(this->exitRates.empty(), storm::exceptions::InvalidArgumentException, "The specified exit rate vector has an unexpected size.");
                    this->exitRates.reserve(this->getNumberOfStates());
                }
                
                storm::utility::ConstantsComparator<ValueType> comparator;
                for (uint_fast64_t state = 0; state< this->getNumberOfStates(); ++state) {
                    uint_fast64_t row = this->getTransitionMatrix().getRowGroupIndices()[state];
                    if (this->markovianStates.get(state)) {
                        if (assertRates) {
                            STORM_LOG_THROW(this->exitRates[state] == this->getTransitionMatrix().getRowSum(row), storm::exceptions::InvalidArgumentException, "The specified exit rate is inconsistent with the rate matrix. Difference is " << (this->exitRates[state] - this->getTransitionMatrix().getRowSum(row)) << ".");
                        } else {
                            this->exitRates.push_back(this->getTransitionMatrix().getRowSum(row));
                        }
                        for (auto& transition : this->getTransitionMatrix().getRow(row)) {
                            transition.setValue(transition.getValue() / this->exitRates[state]);
                        }
                        ++row;
                    } else {
                        if (assertRates) {
                            STORM_LOG_THROW(comparator.isZero(this->exitRates[state]), storm::exceptions::InvalidArgumentException, "The specified exit rate for (non-Markovian) choice should be 0.");
                        } else {
                            this->exitRates.push_back(storm::utility::zero<ValueType>());
                        }
                    }
                    for (; row < this->getTransitionMatrix().getRowGroupIndices()[state+1]; ++row) {
                        STORM_LOG_THROW(comparator.isOne(this->getTransitionMatrix().getRowSum(row)), storm::exceptions::InvalidArgumentException, "Entries of transition matrix do not sum up to one for (non-Markovian) choice " << row << " of state " << state << " (sum is " << this->getTransitionMatrix().getRowSum(row) << ").");
                    }
                }
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::isConvertibleToCtmc() const {
                return isClosed() && markovianStates.full();
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::hasOnlyTrivialNondeterminism() const {
                // Check every state
                for (uint_fast64_t state = 0; state < this->getNumberOfStates(); ++state) {
                    // Get number of choices in current state
                    uint_fast64_t numberChoices = this->getTransitionMatrix().getRowGroupIndices()[state + 1] - this->getTransitionMatrix().getRowGroupIndices()[state];
                    if (isMarkovianState(state)) {
                        STORM_LOG_ASSERT(numberChoices == 1, "Wrong number of choices for Markovian state.");
                    }
                    if (numberChoices > 1) {
                        STORM_LOG_ASSERT(isProbabilisticState(state), "State is not probabilistic.");
                        return false;
                    }
                }
                return true;
            }
            
            template <typename ValueType, typename RewardModelType>
            bool MarkovAutomaton<ValueType, RewardModelType>::checkIsClosed() const {
                for (auto state : markovianStates) {
                    if (this->getTransitionMatrix().getRowGroupSize(state) > 1) {
                        return false;
                    }
                }
                return true;
            }
            
            template <typename ValueType, typename RewardModelType>
            std::shared_ptr<storm::models::sparse::Ctmc<ValueType, RewardModelType>> MarkovAutomaton<ValueType, RewardModelType>::convertToCtmc() const {
                if (isClosed() && markovianStates.full()) {
                    storm::storage::sparse::ModelComponents<ValueType, RewardModelType> components(this->getTransitionMatrix(), this->getStateLabeling(), this->getRewardModels(), false);
                    components.transitionMatrix.makeRowGroupingTrivial();
                    components.exitRates = this->getExitRates();
                    if (this->hasChoiceLabeling()) {
                        components.choiceLabeling = this->getChoiceLabeling();
                    }
                    if (this->hasStateValuations()) {
                        components.stateValuations = this->getStateValuations();
                    }
                    if (this->hasChoiceOrigins()) {
                        components.choiceOrigins = this->getChoiceOrigins();
                    }
                    return std::make_shared<storm::models::sparse::Ctmc<ValueType, RewardModelType>>(std::move(components));
                }
                STORM_LOG_TRACE("MA matrix:" << std::endl << this->getTransitionMatrix());
                STORM_LOG_TRACE("Markovian states: " << getMarkovianStates());

                // Eliminate all probabilistic states by state elimination
                // Initialize
                storm::storage::FlexibleSparseMatrix<ValueType> flexibleMatrix(this->getTransitionMatrix());
                storm::storage::FlexibleSparseMatrix<ValueType> flexibleBackwardTransitions(this->getTransitionMatrix().transpose());
                storm::solver::stateelimination::StateEliminator<ValueType> stateEliminator(flexibleMatrix, flexibleBackwardTransitions);

                for (uint_fast64_t state = 0; state < this->getNumberOfStates(); ++state) {
                    STORM_LOG_ASSERT(!this->isHybridState(state), "State is hybrid.");
                    if (this->isProbabilisticState(state)) {
                        // Eliminate this probabilistic state
                        stateEliminator.eliminateState(state, true);
                        STORM_LOG_TRACE("Flexible matrix after eliminating state " << state << ":" << std::endl << flexibleMatrix);
                    }
                }

                // Create the rate matrix for the CTMC
                storm::storage::SparseMatrixBuilder<ValueType> transitionMatrixBuilder(0, 0, 0, false, false);
                // Remember state to keep
                storm::storage::BitVector keepStates(this->getNumberOfStates(), true);
                for (uint_fast64_t state = 0; state < this->getNumberOfStates(); ++state) {
                    if (storm::utility::isZero(flexibleMatrix.getRowSum(state))) {
                        // State is eliminated and can be discarded
                        keepStates.set(state, false);
                    } else {
                        STORM_LOG_ASSERT(this->isMarkovianState(state), "State is not Markovian.");
                        // Copy transitions
                        for (uint_fast64_t row = flexibleMatrix.getRowGroupIndices()[state]; row < flexibleMatrix.getRowGroupIndices()[state + 1]; ++row) {
                            for (auto const& entry : flexibleMatrix.getRow(row)) {
                                // Convert probabilities into rates
                                transitionMatrixBuilder.addNextValue(state, entry.getColumn(), entry.getValue() * exitRates[state]);
                            }
                        }
                    }
                }

                storm::storage::SparseMatrix<ValueType> rateMatrix = transitionMatrixBuilder.build();
                rateMatrix = rateMatrix.getSubmatrix(false, keepStates, keepStates, false);
                STORM_LOG_TRACE("New CTMC matrix:" << std::endl << rateMatrix);
                // Construct CTMC
                storm::models::sparse::StateLabeling stateLabeling = this->getStateLabeling().getSubLabeling(keepStates);

                //TODO update reward models and choice labels according to kept states
                STORM_LOG_WARN_COND(this->getRewardModels().empty(), "Conversion of MA to CTMC does not preserve rewards.");
                STORM_LOG_WARN_COND(!this->hasChoiceLabeling(), "Conversion of MA to CTMC does not preserve choice labels.");
                STORM_LOG_WARN_COND(!this->hasStateValuations(), "Conversion of MA to CTMC does not preserve choice labels.");
                STORM_LOG_WARN_COND(!this->hasChoiceOrigins(), "Conversion of MA to CTMC does not preserve choice labels.");
                return std::make_shared<storm::models::sparse::Ctmc<ValueType, RewardModelType>>(std::move(rateMatrix), std::move(stateLabeling));
            }

            
            template<typename ValueType, typename RewardModelType>
            void MarkovAutomaton<ValueType, RewardModelType>::printModelInformationToStream(std::ostream& out) const {
                this->printModelInformationHeaderToStream(out);
                out << "Choices: \t" << this->getNumberOfChoices() << std::endl;
                out << "Markovian St.: \t" << this->getMarkovianStates().getNumberOfSetBits() << std::endl;
                out << "Max. Rate.: \t" << this->getMaximalExitRate() << std::endl;
                this->printModelInformationFooterToStream(out);
            }
            
            
            template class MarkovAutomaton<double>;
#ifdef STORM_HAVE_CARL
            template class MarkovAutomaton<storm::RationalNumber>;
            
            template class MarkovAutomaton<double, storm::models::sparse::StandardRewardModel<storm::Interval>>;
            template class MarkovAutomaton<storm::RationalFunction>;
#endif
        } // namespace sparse
    } // namespace models
} // namespace storm
