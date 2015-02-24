#include "src/models/symbolic/Dtmc.h"

namespace storm {
    namespace models {
        namespace symbolic {
            
            template<storm::dd::DdType Type>
            Dtmc<Type>::Dtmc(std::shared_ptr<storm::dd::DdManager<Type>> manager,
                             storm::dd::Dd<Type> reachableStates,
                             storm::dd::Dd<Type> initialStates,
                             storm::dd::Dd<Type> transitionMatrix,
                             std::set<storm::expressions::Variable> const& rowVariables,
                             std::shared_ptr<storm::adapters::DdExpressionAdapter<Type>> rowExpressionAdapter,
                             std::set<storm::expressions::Variable> const& columnVariables,
                             std::shared_ptr<storm::adapters::DdExpressionAdapter<Type>> columnExpressionAdapter,
                             std::vector<std::pair<storm::expressions::Variable, storm::expressions::Variable>> const& rowColumnMetaVariablePairs,
                             std::map<std::string, storm::expressions::Expression> labelToExpressionMap,
                             boost::optional<storm::dd::Dd<Type>> const& optionalStateRewardVector,
                             boost::optional<storm::dd::Dd<Type>> const& optionalTransitionRewardMatrix)
            : DeterministicModel<Type>(storm::models::ModelType::Dtmc, manager, reachableStates, initialStates, transitionMatrix, rowVariables, rowExpressionAdapter, columnVariables, columnExpressionAdapter, rowColumnMetaVariablePairs, labelToExpressionMap, optionalStateRewardVector, optionalTransitionRewardMatrix) {
                // Intentionally left empty.
            }
            
            // Explicitly instantiate the template class.
            template class Dtmc<storm::dd::DdType::CUDD>;
            
        } // namespace symbolic
    } // namespace models
} // namespace storm