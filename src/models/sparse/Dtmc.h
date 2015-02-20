#ifndef STORM_MODELS_SPARSE_DTMC_H_
#define STORM_MODELS_SPARSE_DTMC_H_

#include <ostream>
#include <iostream>
#include <memory>
#include <cstdlib>

#include "src/models/sparse/DeterministicModel.h"
#include "src/models/sparse/StateLabeling.h"
#include "src/storage/SparseMatrix.h"
#include "src/exceptions/InvalidArgumentException.h"
#include "src/exceptions/NotImplementedException.h"
#include "src/settings/SettingsManager.h"
#include "src/utility/OsDetection.h"
#include "src/utility/constants.h"
#include "src/utility/vector.h"
#include "src/utility/macros.h"
#include "src/utility/matrix.h"
#include "src/utility/constants.h"

namespace storm {
    namespace models {
        namespace sparse {
            
            /*!
             * This class represents a discrete-time Markov chain.
             */
            template <class ValueType>
            class Dtmc : public DeterministicModel<ValueType> {
            public:
                /*!
                 * Constructs a model from the given data.
                 *
                 * @param transitionMatrix The matrix representing the transitions in the model.
                 * @param stateLabeling The labeling of the states.
                 * @param optionalStateRewardVector The reward values associated with the states.
                 * @param optionalTransitionRewardMatrix The reward values associated with the transitions of the model.
                 * @param optionalChoiceLabeling A vector that represents the labels associated with the choices of each state.
                 */
                Dtmc(storm::storage::SparseMatrix<ValueType> const& probabilityMatrix,
                     storm::models::sparse::StateLabeling const& stateLabeling,
                     boost::optional<std::vector<ValueType>> const& optionalStateRewardVector = boost::optional<std::vector<ValueType>>(),
                     boost::optional<storm::storage::SparseMatrix<ValueType>> const& optionalTransitionRewardMatrix = boost::optional<storm::storage::SparseMatrix<ValueType>>(),
                     boost::optional<std::vector<boost::container::flat_set<uint_fast64_t>>> const& optionalChoiceLabeling = boost::optional<std::vector<boost::container::flat_set<uint_fast64_t>>>());
                
                /*!
                 * Constructs a model by moving the given data.
                 *
                 * @param transitionMatrix The matrix representing the transitions in the model.
                 * @param stateLabeling The labeling of the states.
                 * @param optionalStateRewardVector The reward values associated with the states.
                 * @param optionalTransitionRewardMatrix The reward values associated with the transitions of the model.
                 * @param optionalChoiceLabeling A vector that represents the labels associated with the choices of each state.
                 */
                Dtmc(storm::storage::SparseMatrix<ValueType>&& probabilityMatrix, storm::models::sparse::StateLabeling&& stateLabeling,
                     boost::optional<std::vector<ValueType>>&& optionalStateRewardVector = boost::optional<std::vector<ValueType>>(),
                     boost::optional<storm::storage::SparseMatrix<ValueType>>&& optionalTransitionRewardMatrix = boost::optional<storm::storage::SparseMatrix<ValueType>>(),
                     boost::optional<std::vector<boost::container::flat_set<uint_fast64_t>>>&& optionalChoiceLabeling = boost::optional<std::vector<boost::container::flat_set<uint_fast64_t>>>());
                
                Dtmc(Dtmc<ValueType> const& dtmc) = default;
                Dtmc& operator=(Dtmc<ValueType> const& dtmc) = default;
                
#ifndef WINDOWS
                Dtmc(Dtmc<ValueType>&& dtmc) = default;
                Dtmc& operator=(Dtmc<ValueType>&& dtmc) = default;
#endif
                
                /*!
                 * Retrieves the sub-DTMC that only contains the given set of states.
                 *
                 * @param states The states of the sub-DTMC.
                 * @return The resulting sub-DTMC.
                 */
                Dtmc<ValueType> getSubDtmc(storm::storage::BitVector const& states) const;
                
            private:
                /*!
                 * Checks the probability matrix for validity.
                 *
                 * @return True iff the probability matrix is valid.
                 */
                bool checkValidityOfProbabilityMatrix() const;
            };
            
        } // namespace sparse
    } // namespace models
} // namespace storm

#endif /* STORM_MODELS_SPARSE_DTMC_H_ */
