#include "DFTState.h"
#include "storm-dft/storage/dft/DFTElements.h"
#include "storm-dft/storage/dft/DFT.h"
#include "storm/exceptions/InvalidArgumentException.h"

namespace storm {
    namespace storage {

        template<typename ValueType>
        DFTState<ValueType>::DFTState(DFT<ValueType> const& dft, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) : mStatus(dft.stateBitVectorSize()), mId(id), failableElements(dft.nrElements(), dft.getRelevantEvents()), mPseudoState(false), mDft(dft), mStateGenerationInfo(stateGenerationInfo) {
            // TODO: use construct()
            
            // Initialize uses
            for(size_t spareId : mDft.getSpareIndices()) {
                std::shared_ptr<DFTGate<ValueType> const> elem = mDft.getGate(spareId);
                STORM_LOG_ASSERT(elem->isSpareGate(), "Element is no spare gate.");
                STORM_LOG_ASSERT(elem->nrChildren() > 0, "Element has no child.");
                this->setUses(spareId, elem->children()[0]->id());
            }

            // Initialize activation
            propagateActivation(mDft.getTopLevelIndex());

            // Initialize currently failable BEs
            for (size_t id : mDft.nonColdBEs()) {
                // Check if restriction might prevent failure
                if (!isEventDisabledViaRestriction(id)) {
                    failableElements.addBE(id);
                } else {
                    STORM_LOG_TRACE("BE " << id << " is disabled due to a restriction.");
                }
            }
        }

        template<typename ValueType>
        DFTState<ValueType>::DFTState(storm::storage::BitVector const& status, DFT<ValueType> const& dft, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) : mStatus(status), mId(id), failableElements(dft.nrElements(), dft.getRelevantEvents()), mPseudoState(true), mDft(dft), mStateGenerationInfo(stateGenerationInfo) {
            // Intentionally left empty
        }
        
        template<typename ValueType>
        void DFTState<ValueType>::construct() {
            STORM_LOG_TRACE("Construct concrete state from pseudo state " << mDft.getStateString(mStatus, mStateGenerationInfo, mId));
            // Clear information from pseudo state
            failableElements.clear();
            mUsedRepresentants.clear();
            STORM_LOG_ASSERT(mPseudoState, "Only pseudo states can be constructed.");
            for(size_t index = 0; index < mDft.nrElements(); ++index) {
                // Initialize currently failable BE
                if (mDft.isBasicElement(index) && isOperational(index) && !isEventDisabledViaRestriction(index)) {
                    std::shared_ptr<const DFTBE<ValueType>> be = mDft.getBasicElement(index);
                    if (be->canFail()) {
                        switch (be->type()) {
                            case storm::storage::DFTElementType::BE_EXP:
                            {
                                auto beExp = std::static_pointer_cast<BEExponential<ValueType> const>(be);
                                if (!beExp->isColdBasicElement() || !mDft.hasRepresentant(index) || isActive(mDft.getRepresentant(index))) {
                                    failableElements.addBE(index);
                                    STORM_LOG_TRACE("Currently failable: " << *beExp);
                                }
                                break;
                            }
                            case storm::storage::DFTElementType::BE_CONST:
                                failableElements.addBE(index);
                                STORM_LOG_TRACE("Currently failable: " << *be);
                                break;
                            default:
                                STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "BE type '" << be->type() << "' is not supported.");
                                break;
                        }
                    }
                } else if (mDft.getElement(index)->isSpareGate()) {
                    // Initialize used representants
                    uint_fast64_t useId = uses(index);
                    mUsedRepresentants.push_back(useId);
                    STORM_LOG_TRACE("Spare " << index << " uses " << useId);
                }
            }

            // Initialize failable dependencies
            for (size_t dependencyId : mDft.getDependencies()) {
                std::shared_ptr<DFTDependency<ValueType> const> dependency = mDft.getDependency(dependencyId);
                STORM_LOG_ASSERT(dependencyId == dependency->id(), "Ids do not match.");
                assert(dependency->dependentEvents().size() == 1);
                if (hasFailed(dependency->triggerEvent()->id()) && getElementState(dependency->dependentEvents()[0]->id()) == DFTElementState::Operational) {
                    failableElements.addDependency(dependencyId);
                    STORM_LOG_TRACE("New dependency failure: " << *dependency);
                }
            }

            // Initialize remaining relevant events
            failableElements.remainingRelevantEvents = mDft.getRelevantEvents();
            this->updateRemainingRelevantEvents();

            mPseudoState = false;
        }

        template<typename ValueType>
        std::shared_ptr<DFTState<ValueType>> DFTState<ValueType>::copy() const {
            return std::make_shared<storm::storage::DFTState<ValueType>>(*this);
        }

        template<typename ValueType>
        DFTElementState DFTState<ValueType>::getElementState(size_t id) const {
            return static_cast<DFTElementState>(getElementStateInt(id));
        }

        template<typename ValueType>
        DFTElementState DFTState<ValueType>::getElementState(storm::storage::BitVector const& state, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) {
            return static_cast<DFTElementState>(DFTState<ValueType>::getElementStateInt(state, stateGenerationInfo, id));
        }
        
        template<typename ValueType>
        DFTDependencyState DFTState<ValueType>::getDependencyState(size_t id) const {
            return static_cast<DFTDependencyState>(getElementStateInt(id));
        }

        template<typename ValueType>
        DFTDependencyState DFTState<ValueType>::getDependencyState(storm::storage::BitVector const& state, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) {
            return static_cast<DFTDependencyState>(DFTState<ValueType>::getElementStateInt(state, stateGenerationInfo, id));
        }

        template<typename ValueType>
        int DFTState<ValueType>::getElementStateInt(size_t id) const {
            return mStatus.getAsInt(mStateGenerationInfo.getStateIndex(id), 2);
        }
        
        template<typename ValueType>
        int DFTState<ValueType>::getElementStateInt(storm::storage::BitVector const& state, DFTStateGenerationInfo const& stateGenerationInfo, size_t id) {
            return state.getAsInt(stateGenerationInfo.getStateIndex(id), 2);
        }

        template<typename ValueType>
        size_t DFTState<ValueType>::getId() const {
            return mId;
        }

        template<typename ValueType>
        void DFTState<ValueType>::setId(size_t id) {
            mId = id;
        }

        template<typename ValueType>
        bool DFTState<ValueType>::isOperational(size_t id) const {
            return getElementState(id) == DFTElementState::Operational;
        }

        template<typename ValueType>
        bool DFTState<ValueType>::hasFailed(size_t id) const {
            return mStatus[mStateGenerationInfo.getStateIndex(id)];
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::hasFailed(storm::storage::BitVector const& state, size_t indexId) {
            return state[indexId];
        }

        template<typename ValueType>
        bool DFTState<ValueType>::isFailsafe(size_t id) const {
            return mStatus[mStateGenerationInfo.getStateIndex(id)+1];
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::isFailsafe(storm::storage::BitVector const& state, size_t indexId) {
            return state[indexId+1];
        }

        template<typename ValueType>
        bool DFTState<ValueType>::dontCare(size_t id) const {
            return getElementState(id) == DFTElementState::DontCare;
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::dependencyTriggered(size_t id) const {
            return getElementStateInt(id) > 0;
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::dependencySuccessful(size_t id) const {
            return mStatus[mStateGenerationInfo.getStateIndex(id)];
        }
        template<typename ValueType>
        bool DFTState<ValueType>::dependencyUnsuccessful(size_t id) const {
            return mStatus[mStateGenerationInfo.getStateIndex(id)+1];
        }

        template<typename ValueType>
        void DFTState<ValueType>::setFailed(size_t id) {
            mStatus.set(mStateGenerationInfo.getStateIndex(id));
        }

        template<typename ValueType>
        void DFTState<ValueType>::setFailsafe(size_t id) {
            mStatus.set(mStateGenerationInfo.getStateIndex(id)+1);
        }

        template<typename ValueType>
        void DFTState<ValueType>::setDontCare(size_t id) {
            if (mDft.isRepresentative(id)) {
                // Activate dont care element
                activate(id);
            }
            mStatus.setFromInt(mStateGenerationInfo.getStateIndex(id), 2, static_cast<uint_fast64_t>(DFTElementState::DontCare) );
        }
        
        template<typename ValueType>
        void DFTState<ValueType>::setDependencySuccessful(size_t id) {
            // Only distinguish between passive and dont care dependencies
            //mStatus.set(mStateGenerationInfo.getStateIndex(id));
            setDependencyDontCare(id);
        }

        template<typename ValueType>
        void DFTState<ValueType>::setDependencyUnsuccessful(size_t id) {
            // Only distinguish between passive and dont care dependencies
            //mStatus.set(mStateGenerationInfo.getStateIndex(id)+1);
            setDependencyDontCare(id);
        }
        
        template<typename ValueType>
        void DFTState<ValueType>::setDependencyDontCare(size_t id) {
            mStatus.setFromInt(mStateGenerationInfo.getStateIndex(id), 2, static_cast<uint_fast64_t>(DFTDependencyState::DontCare));
        }

        template<typename ValueType>
        void DFTState<ValueType>::beNoLongerFailable(size_t id) {
            failableElements.removeBE(id);
            updateDontCareDependencies(id);
        }

        template<typename ValueType>
        bool DFTState<ValueType>::updateFailableDependencies(size_t id) {
            if (!hasFailed(id)) {
                return false;
            }

            bool addedFailableDependency = false;
            for (auto dependency : mDft.getElement(id)->outgoingDependencies()) {
                STORM_LOG_ASSERT(dependency->triggerEvent()->id() == id, "Ids do not match.");
                STORM_LOG_ASSERT(dependency->dependentEvents().size() == 1, "Only one dependent event is allowed.");
                if (getElementState(dependency->dependentEvents()[0]->id()) == DFTElementState::Operational) {
                    STORM_LOG_ASSERT(!isFailsafe(dependency->dependentEvents()[0]->id()), "Dependent event is failsafe.");
                    // By assertion we have only one dependent event
                    // Check if restriction prevents failure of dependent event
                    if (!isEventDisabledViaRestriction(dependency->dependentEvents()[0]->id())) {
                        // Add dependency as possible failure
                        failableElements.addDependency(dependency->id());
                        STORM_LOG_TRACE("New dependency failure: " << *dependency);
                        addedFailableDependency = true;
                    }
                }
            }
            return addedFailableDependency;
        }

        template<typename ValueType>
        bool DFTState<ValueType>::updateFailableInRestrictions(size_t id) {
            if (!hasFailed(id)) {
                // Non-failure does not change anything in a restriction
                return false;
            }

            bool addedFailableEvent = false;
            for (auto restriction : mDft.getElement(id)->restrictions()) {
                STORM_LOG_ASSERT(restriction->containsChild(id), "Ids do not match.");
                if (restriction->isSeqEnforcer()) {
                    for (auto it = restriction->children().cbegin(); it != restriction->children().cend(); ++it) {
                        if ((*it)->id() != id) {
                            if (!hasFailed((*it)->id())) {
                                // Failure should be prevented later on
                                STORM_LOG_TRACE("Child " << (*it)->name() << " should have failed.");
                                break;
                            }
                        } else {
                            // Current event has failed
                            STORM_LOG_ASSERT(hasFailed((*it)->id()), "Child " << (*it)->name() << " should have failed.");
                            ++it;
                            while (it != restriction->children().cend() && !isOperational((*it)->id())) {
                                ++it;
                            }
                            if (it != restriction->children().cend() && (*it)->isBasicElement() && isOperational((*it)->id())) {
                                // Enable next event
                                failableElements.addBE((*it)->id());
                                STORM_LOG_TRACE("Added possible BE failure: " << *(*it));
                                addedFailableEvent = true;
                            }
                            break;
                        }
                    }
                } else if (restriction->isMutex()) {
                    // Current element has failed and disables all other children
                    for (auto const& child : restriction->children()) {
                        if (child->isBasicElement() && child->id() != id && getElementState(child->id()) == DFTElementState::Operational) {
                            // Disable child
                            failableElements.removeBE(child->id());
                            STORM_LOG_TRACE("Disabled child: " << *child);
                            addedFailableEvent = true;
                        }
                    }
                } else {
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "Restriction must be SEQ or MUTEX");
                }
            }
            return addedFailableEvent;
        }
        
        template<typename ValueType>
        void DFTState<ValueType>::updateDontCareDependencies(size_t id) {
            STORM_LOG_ASSERT(mDft.isBasicElement(id), "Element is no BE.");
            STORM_LOG_ASSERT(hasFailed(id), "Element has not failed.");
            
            for (auto dependency : mDft.getBasicElement(id)->ingoingDependencies()) {
                assert(dependency->dependentEvents().size() == 1);
                STORM_LOG_ASSERT(dependency->dependentEvents()[0]->id() == id, "Ids do not match.");
                setDependencyDontCare(dependency->id());
                failableElements.removeDependency(dependency->id());
            }
        }

        template<typename ValueType>
        void DFTState<ValueType>::updateRemainingRelevantEvents() {
            for (auto it = failableElements.remainingRelevantEvents.begin(); it != failableElements.remainingRelevantEvents.end(); ) {
                if (isOperational(*it)) {
                    ++it;
                } else {
                    // Element is not relevant anymore
                    it = failableElements.remainingRelevantEvents.erase(it);
                }
            }
        }

        template<typename ValueType>
        ValueType DFTState<ValueType>::getBERate(size_t id) const {
            STORM_LOG_ASSERT(mDft.isBasicElement(id), "Element is no BE.");
            STORM_LOG_THROW(mDft.getBasicElement(id)->type() == storm::storage::DFTElementType::BE_EXP, storm::exceptions::NotSupportedException, "BE of type '" << mDft.getBasicElement(id)->type() << "' is not supported.");
            auto beExp = std::static_pointer_cast<storm::storage::BEExponential<ValueType> const>(mDft.getBasicElement(id));
            if (mDft.hasRepresentant(id) && !isActive(mDft.getRepresentant(id))) {
                // Return passive failure rate
                return beExp->passiveFailureRate();
            } else {
                // Return active failure rate
                return beExp->activeFailureRate();
            }
        }

        template<typename ValueType>
        std::pair<std::shared_ptr<DFTBE<ValueType> const>, bool> DFTState<ValueType>::letNextBEFail(size_t id, bool dueToDependency) {
            STORM_LOG_TRACE("currently failable: " << getCurrentlyFailableString());
            if (dueToDependency) {
                // Consider failure due to dependency
                std::shared_ptr<DFTDependency<ValueType> const> dependency = mDft.getDependency(id);
                STORM_LOG_ASSERT(dependency->dependentEvents().size() == 1, "More than one dependent event");
                std::pair<std::shared_ptr<DFTBE<ValueType> const>,bool> res(mDft.getBasicElement(dependency->dependentEvents()[0]->id()), true);
                STORM_LOG_ASSERT(!hasFailed(res.first->id()), "Element " << *(res.first) << " has already failed.");
                failableElements.removeDependency(id);
                setFailed(res.first->id());
                setDependencySuccessful(dependency->id());
                beNoLongerFailable(res.first->id());
                return res;
            } else {
                // Consider "normal" failure
                std::pair<std::shared_ptr<DFTBE<ValueType> const>,bool> res(mDft.getBasicElement(id), false);
                STORM_LOG_ASSERT(!hasFailed(res.first->id()), "Element " << *(res.first) << " has already failed.");
                STORM_LOG_ASSERT(res.first->canFail(), "Element " << *(res.first) << " cannot fail.");
                failableElements.removeBE(id);
                setFailed(res.first->id());
                return res;
            }
        }
 
        template<typename ValueType>
        void DFTState<ValueType>::letDependencyBeUnsuccessful(size_t id) {
            STORM_LOG_ASSERT(failableElements.hasDependencies(), "Index invalid.");
            std::shared_ptr<DFTDependency<ValueType> const> dependency = mDft.getDependency(id);
            failableElements.removeDependency(id);
            setDependencyUnsuccessful(dependency->id());
        }

        template<typename ValueType>
        void DFTState<ValueType>::activate(size_t repr) {
            size_t activationIndex = mStateGenerationInfo.getSpareActivationIndex(repr);
            mStatus.set(activationIndex);
        }

        template<typename ValueType>
        bool DFTState<ValueType>::isActive(size_t id) const {
            STORM_LOG_ASSERT(mDft.isRepresentative(id), "Element " << *(mDft.getElement(id)) << " is no representative.");
            return mStatus[mStateGenerationInfo.getSpareActivationIndex(id)];
        }
            
        template<typename ValueType>
        void DFTState<ValueType>::propagateActivation(size_t representativeId) {
            if (representativeId != mDft.getTopLevelIndex()) {
                activate(representativeId);
            }
            for(size_t elem : mDft.module(representativeId)) {
                if(mDft.isBasicElement(elem) && isOperational(elem) && !isEventDisabledViaRestriction(elem)) {
                    std::shared_ptr<const DFTBE<ValueType>> be = mDft.getBasicElement(elem);
                    if (be->canFail()) {
                        switch (be->type()) {
                            case storm::storage::DFTElementType::BE_EXP: {
                                auto beExp = std::static_pointer_cast<BEExponential<ValueType> const>(be);
                                if (beExp->isColdBasicElement()) {
                                    // Add to failable BEs
                                    failableElements.addBE(elem);
                                }
                                break;
                            }
                            case storm::storage::DFTElementType::BE_CONST:
                                // Nothing to do
                                break;
                            default:
                                STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "BE type '" << be->type() << "' is not supported.");
                        }
                    }
                } else if (mDft.getElement(elem)->isSpareGate() && !isActive(uses(elem))) {
                    propagateActivation(uses(elem));
                }
            }
        }

        template<typename ValueType>
        uint_fast64_t DFTState<ValueType>::uses(size_t id) const {
            size_t nrUsedChild = extractUses(mStateGenerationInfo.getSpareUsageIndex(id));
            if (nrUsedChild == mDft.getMaxSpareChildCount()) {
                return id;
            } else {
                return mDft.getChild(id, nrUsedChild);
            }
        }

        template<typename ValueType>
        uint_fast64_t DFTState<ValueType>::extractUses(size_t from) const {
            STORM_LOG_ASSERT(mStateGenerationInfo.usageInfoBits() < 64, "UsageInfoBit size too large.");
            return mStatus.getAsInt(from, mStateGenerationInfo.usageInfoBits());
        }

        template<typename ValueType>
        bool DFTState<ValueType>::isUsed(size_t child) const {
            return (std::find(mUsedRepresentants.begin(), mUsedRepresentants.end(), child) != mUsedRepresentants.end());
        }

        template<typename ValueType>
        void DFTState<ValueType>::setUses(size_t spareId, size_t child) {
            mStatus.setFromInt(mStateGenerationInfo.getSpareUsageIndex(spareId), mStateGenerationInfo.usageInfoBits(), mDft.getNrChild(spareId, child));
            mUsedRepresentants.push_back(child);
        }
        
        template<typename ValueType>
        void DFTState<ValueType>::finalizeUses(size_t spareId) {
            STORM_LOG_ASSERT(hasFailed(spareId), "Spare has not failed.");
            mStatus.setFromInt(mStateGenerationInfo.getSpareUsageIndex(spareId), mStateGenerationInfo.usageInfoBits(), mDft.getMaxSpareChildCount());
        }

        template<typename ValueType>
        bool DFTState<ValueType>::isEventDisabledViaRestriction(size_t id) const {
            STORM_LOG_ASSERT(!mDft.isDependency(id), "Event " << id << " is dependency.");
            STORM_LOG_ASSERT(!mDft.isRestriction(id), "Event " << id << " is restriction.");
            // First check sequence enforcer
            auto const& preIds = mStateGenerationInfo.seqRestrictionPreElements(id);
            for (size_t id : preIds) {
                if (isOperational(id)) {
                    return true;
                }
            }

            // Second check mutexes
            auto const& mutexIds = mStateGenerationInfo.mutexRestrictionElements(id);
            for (size_t id : mutexIds) {
                if (!isOperational(id)) {
                    return true;
                }
            }
            return false;
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::hasOperationalPostSeqElements(size_t id) const {
            STORM_LOG_ASSERT(!mDft.isDependency(id), "Element is dependency.");
            STORM_LOG_ASSERT(!mDft.isRestriction(id), "Element is restriction.");
            auto const& postIds =  mStateGenerationInfo.seqRestrictionPostElements(id);
            for(size_t id : postIds) {
                if(isOperational(id)) {
                    return true;
                }
            }
            return false;
        }

        template<typename ValueType>
        bool DFTState<ValueType>::claimNew(size_t spareId, size_t currentlyUses, std::vector<std::shared_ptr<DFTElement<ValueType>>> const& children) {
            auto it = children.begin();
            while ((*it)->id() != currentlyUses) {
                STORM_LOG_ASSERT(it != children.end(), "Currently used element not found.");
                ++it;
            }
            ++it;
            while(it != children.end()) {
                size_t childId = (*it)->id();
                if(!hasFailed(childId) && !isUsed(childId)) {
                    setUses(spareId, childId);
                    if(isActive(currentlyUses)) {
                        propagateActivation(childId);
                    }
                    return true;
                }
                ++it;
            }
            return false;
        }
        
        template<typename ValueType>
        bool DFTState<ValueType>::orderBySymmetry() {
            bool changed = false;
            for (size_t pos = 0; pos < mStateGenerationInfo.getSymmetrySize(); ++pos) {
                // Check each symmetry
                size_t length = mStateGenerationInfo.getSymmetryLength(pos);
                std::vector<size_t> symmetryIndices = mStateGenerationInfo.getSymmetryIndices(pos);
                // Sort symmetry group in decreasing order by bubble sort
                // TODO use better algorithm?
                size_t tmp;
                size_t n = symmetryIndices.size();
                do {
                    tmp = 0;
                    for (size_t i = 1; i < n; ++i) {
                        STORM_LOG_ASSERT(symmetryIndices[i-1] + length <= mStatus.size(), "Symmetry index "<< symmetryIndices[i-1] << " + length " << length << " is larger than status vector " << mStatus.size());
                        STORM_LOG_ASSERT(symmetryIndices[i] + length <= mStatus.size(), "Symmetry index "<< symmetryIndices[i] << " + length " << length << " is larger than status vector " << mStatus.size());
                        if (mStatus.compareAndSwap(symmetryIndices[i-1], symmetryIndices[i], length)) {
                            tmp = i;
                            changed = true;
                        }
                    }
                    n = tmp;
                } while (n > 0);
            }
            if (changed) {
                mPseudoState = true;
            }
            return changed;
        }


        // Explicitly instantiate the class.
        template class DFTState<double>;

#ifdef STORM_HAVE_CARL
        template class DFTState<RationalFunction>;
#endif

    }
}
