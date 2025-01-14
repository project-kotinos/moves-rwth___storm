#pragma  once

#include <memory>
#include <unordered_map>
#include <list>
#include <map>
#include <vector>

#include <boost/iterator/counting_iterator.hpp>

#include "storm/storage/BitVector.h"
#include "storm/utility/math.h"
#include "storm/utility/macros.h"
#include "storm/exceptions/NotSupportedException.h"

#include "storm-dft/storage/dft/DFTElements.h"
#include "storm-dft/storage/dft/SymmetricUnits.h"
#include "storm-dft/storage/dft/DFTStateGenerationInfo.h"
#include "storm-dft/storage/dft/DFTLayoutInfo.h"

namespace storm {
    namespace builder {
        // Forward declaration
        template<typename T> class DFTBuilder;
    }

    namespace storage {

        template<typename ValueType>
        struct DFTElementSort {
            bool operator()(std::shared_ptr<DFTElement<ValueType>> const& a, std::shared_ptr<DFTElement<ValueType>> const& b)  const {
                if (a->rank() == 0 && b->rank() == 0) {
                    return a->isConstant();
                } else {
                    return a->rank() < b->rank();
                }
            }
        };


        // Forward declaration
        template<typename T> class DFTColouring;

        /**
         * Represents a Dynamic Fault Tree
         */
        template<typename ValueType>
        class DFT {

            using DFTElementPointer = std::shared_ptr<DFTElement<ValueType>>;
            using DFTElementCPointer = std::shared_ptr<DFTElement<ValueType> const>;
            using DFTElementVector = std::vector<DFTElementPointer>;
            using DFTGatePointer = std::shared_ptr<DFTGate<ValueType>>;
            using DFTGateVector = std::vector<DFTGatePointer>;
            using DFTStatePointer = std::shared_ptr<DFTState<ValueType>>;

        private:
            DFTElementVector mElements;
            size_t mNrOfBEs;
            size_t mNrOfSpares;
            size_t mNrRepresentatives;
            size_t mTopLevelIndex;
            size_t mStateVectorSize;
            size_t mMaxSpareChildCount;
            std::map<size_t, std::vector<size_t>> mSpareModules;
            std::vector<size_t> mDependencies;
            std::vector<size_t> mTopModule;
            std::map<size_t, size_t> mRepresentants; // id element -> id representative
            std::vector<std::vector<size_t>> mSymmetries;
            std::map<size_t, DFTLayoutInfo> mLayoutInfo;

        public:
            DFT(DFTElementVector const& elements, DFTElementPointer const& tle);
            
            DFTStateGenerationInfo buildStateGenerationInfo(storm::storage::DFTIndependentSymmetries const& symmetries) const;
            
            size_t generateStateInfo(DFTStateGenerationInfo& generationInfo, size_t id, storm::storage::BitVector& visited, size_t stateIndex) const;

            size_t performStateGenerationInfoDFS(DFTStateGenerationInfo& generationInfo, std::queue<size_t>& visitQueue, storm::storage::BitVector& visited, size_t stateIndex) const;
        
            DFT<ValueType> optimize() const;
            
            void copyElements(std::vector<size_t> elements, storm::builder::DFTBuilder<ValueType> builder) const;
            
            size_t stateBitVectorSize() const {
                // Ensure multiple of 64
                return (mStateVectorSize / 64 + (mStateVectorSize % 64 != 0)) * 64;
            }
            
            size_t nrElements() const {
                return mElements.size();
            }
            
            size_t nrBasicElements() const {
                return mNrOfBEs;
            }

            size_t nrDynamicElements() const;

            size_t nrStaticElements() const;
            
            size_t getTopLevelIndex() const {
                return mTopLevelIndex;
            }
            
            DFTElementType topLevelType() const {
                return mElements[getTopLevelIndex()]->type();
            }
            
            size_t getMaxSpareChildCount() const {
                return mMaxSpareChildCount;
            }
            
            std::vector<size_t> getSpareIndices() const {
                std::vector<size_t> indices;
                for(auto const& elem : mElements) {
                    if(elem->isSpareGate()) {
                        indices.push_back(elem->id());
                    }
                }
                return indices;
            }
            
            std::vector<size_t> const& module(size_t representativeId) const {
                if(representativeId == mTopLevelIndex) {
                    return mTopModule;
                } else {
                    STORM_LOG_ASSERT(mSpareModules.count(representativeId) > 0, "Representative not found.");
                    return mSpareModules.find(representativeId)->second;
                }
            }
            
            std::vector<size_t> const& getDependencies() const {
                return mDependencies;
            }

            std::vector<size_t> nonColdBEs() const {
                std::vector<size_t> result;
                for (DFTElementPointer elem : mElements) {
                    if (elem->isBasicElement()) {
                        std::shared_ptr<DFTBE<ValueType>> be = std::static_pointer_cast<DFTBE<ValueType>>(elem);
                        if (be->canFail()) {
                            switch (be->type()) {
                                case storm::storage::DFTElementType::BE_EXP: {
                                    auto beExp = std::static_pointer_cast<BEExponential<ValueType>>(be);
                                    if (!beExp->isColdBasicElement()) {
                                        result.push_back(be->id());
                                    }
                                    break;
                                }
                                case storm::storage::DFTElementType::BE_CONST:
                                    result.push_back(be->id());
                                    break;
                                default:
                                    STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "BE type '" << be->type() << "' is not supported.");
                            }
                        }
                    }
                }
                return result;
            }

            /**
             *  Get a pointer to an element in the DFT
             *  @param index The id of the element
             */
            DFTElementCPointer getElement(size_t index) const {
                STORM_LOG_ASSERT(index < nrElements(), "Index invalid.");
                return mElements[index];
            }

            bool isBasicElement(size_t index) const {
                return getElement(index)->isBasicElement();
            }

            bool isGate(size_t index) const {
                return getElement(index)->isGate();
            }

            bool isDependency(size_t index) const {
                return getElement(index)->isDependency();
            }
            
            bool isRestriction(size_t index) const {
                return getElement(index)->isRestriction();
            }

            std::shared_ptr<DFTBE<ValueType> const> getBasicElement(size_t index) const {
                STORM_LOG_ASSERT(isBasicElement(index), "Element is no BE.");
                return std::static_pointer_cast<DFTBE<ValueType> const>(mElements[index]);
            }

            std::shared_ptr<DFTGate<ValueType> const> getTopLevelGate() const {
                return getGate(mTopLevelIndex);
            }
            
            std::shared_ptr<DFTGate<ValueType> const> getGate(size_t index) const {
                STORM_LOG_ASSERT(isGate(index), "Element is no gate.");
                return std::static_pointer_cast<DFTGate<ValueType> const>(mElements[index]);
            }

            std::shared_ptr<DFTDependency<ValueType> const> getDependency(size_t index) const {
                STORM_LOG_ASSERT(isDependency(index), "Element is no dependency.");
                return std::static_pointer_cast<DFTDependency<ValueType> const>(mElements[index]);
            }
            
            std::shared_ptr<DFTRestriction<ValueType> const> getRestriction(size_t index) const {
                STORM_LOG_ASSERT(isRestriction(index), "Element is no restriction.");
                return std::static_pointer_cast<DFTRestriction<ValueType> const>(mElements[index]);
            }

            std::vector<std::shared_ptr<DFTBE<ValueType>>> getBasicElements() const {
                std::vector<std::shared_ptr<DFTBE<ValueType>>> elements;
                for (DFTElementPointer elem : mElements) {
                    if (elem->isBasicElement()) {
                        elements.push_back(std::static_pointer_cast<DFTBE<ValueType>>(elem));
                    }
                }
                return elements;
            }

            bool canHaveNondeterminism() const;

            /*!
             * Check if the DFT is well-formed.
             * @param stream Output stream where warnings about non-well-formed parts are written.
             * @return True iff the DFT is well-formed.
             */
            bool checkWellFormedness(std::ostream& stream) const;

            uint64_t maxRank() const;
            
            std::vector<DFT<ValueType>> topModularisation() const;
            
            bool isRepresentative(size_t id) const {
                for (auto const& parent : getElement(id)->parents()) {
                    if (parent->isSpareGate()) {
                        return true;
                    }
                }
                return false;
            }

            bool hasRepresentant(size_t id) const {
                return mRepresentants.find(id) != mRepresentants.end();
            }

            size_t getRepresentant(size_t id) const {
                STORM_LOG_ASSERT(hasRepresentant(id), "Element has no representant.");
                return mRepresentants.find(id)->second;
            }

            bool hasFailed(DFTStatePointer const& state) const {
                return state->hasFailed(mTopLevelIndex);
            }
            
            bool hasFailed(storm::storage::BitVector const& state, DFTStateGenerationInfo const& stateGenerationInfo) const {
                return storm::storage::DFTState<ValueType>::hasFailed(state, stateGenerationInfo.getStateIndex(mTopLevelIndex));
            }
            
            bool isFailsafe(DFTStatePointer const& state) const {
                return state->isFailsafe(mTopLevelIndex);
            }
            
            bool isFailsafe(storm::storage::BitVector const& state, DFTStateGenerationInfo const& stateGenerationInfo) const {
                return storm::storage::DFTState<ValueType>::isFailsafe(state, stateGenerationInfo.getStateIndex(mTopLevelIndex));
            }
            
            size_t getChild(size_t spareId, size_t nrUsedChild) const;
            
            size_t getNrChild(size_t spareId, size_t childId) const;
            
            std::string getElementsString() const;

            std::string getInfoString() const;

            std::string getSpareModulesString() const;

            std::string getElementsWithStateString(DFTStatePointer const& state) const;

            std::string getStateString(DFTStatePointer const& state) const;

            std::string getStateString(storm::storage::BitVector const& status, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) const;

            std::vector<size_t> getIndependentSubDftRoots(size_t index) const;

            DFTColouring<ValueType> colourDFT() const;
            
            std::map<size_t, size_t> findBijection(size_t index1, size_t index2, DFTColouring<ValueType> const& colouring, bool sparesAsLeaves) const;

            DFTIndependentSymmetries findSymmetries(DFTColouring<ValueType> const& colouring) const;

            void findSymmetriesHelper(std::vector<size_t> const& candidates, DFTColouring<ValueType> const& colouring, std::map<size_t, std::vector<std::vector<size_t>>>& result) const;

            std::vector<size_t> immediateFailureCauses(size_t index) const;
            
            std::vector<size_t> findModularisationRewrite() const;

            void setElementLayoutInfo(size_t id, DFTLayoutInfo const& layoutInfo) {
                mLayoutInfo[id] = layoutInfo;
            }

            DFTLayoutInfo const& getElementLayoutInfo(size_t id) const {
                return mLayoutInfo.at(id);
            }

            void writeStatsToStream(std::ostream& stream) const;

            /*!
             * Get Ids of all elements.
             * @return All element ids.
             */
            std::set<size_t> getAllIds() const;

            /*!
             * Get id for the given element name.
             * @param name Name of element.
             * @return Index of element.
             */
            size_t getIndex(std::string const& name) const;

            /*!
             * Get all relevant events.
             * @return List of all relevant events.
             */
            std::set<size_t> getRelevantEvents() const;

            /*!
             * Set the relevance flag for all elements according to the given relevant events.
             * @param relevantEvents All elements which should be to relevant. All elements not occuring are set to irrelevant.
             * @param allowDCForRelevantEvents Flag whether Don't Care propagation is allowed even for relevant events.
             */
            void setRelevantEvents(std::set<size_t> const& relevantEvents, bool allowDCForRelevantEvents) const;

            /*!
             * Get a string containing the list of all relevant events.
             * @return String containing all relevant events.
             */
            std::string getRelevantEventsString() const;

        private:
            std::tuple<std::vector<size_t>, std::vector<size_t>, std::vector<size_t>> getSortedParentAndDependencyIds(size_t index) const;
            
            bool elementIndicesCorrect() const {
                for(size_t i = 0; i < mElements.size(); ++i) {
                    if(mElements[i]->id() != i) return false;
                }
                return true;
            }

        };
       
    }
}
