#pragma once

#include "storm/modelchecker/propositional/SymbolicPropositionalModelChecker.h"

#include "storm/storage/dd/DdType.h"

namespace storm {
    namespace modelchecker {
        
        template<typename ModelType>
        class PartialBisimulationMdpModelChecker : public SymbolicPropositionalModelChecker<ModelType> {
        public:
            typedef typename ModelType::ValueType ValueType;
            
            /*!
             * Constructs a model checker for the given model.
             */
            explicit PartialBisimulationMdpModelChecker(ModelType const& model);
            
//            /// Overridden methods from super class.
            virtual bool canHandle(CheckTask<storm::logic::Formula> const& checkTask) const override;
            virtual std::unique_ptr<CheckResult> computeUntilProbabilities(CheckTask<storm::logic::UntilFormula> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeReachabilityProbabilities(CheckTask<storm::logic::EventuallyFormula> const& checkTask) override;
        };
    }
}
