#include "MprTagTraceStatement.h"
#include "MprPositionStatement.h"
#include "MprPositionList.h"
#include "MprStatementRegistration.h"
#include <cnoid/BodyKinematicsKit>
#include <cnoid/KinematicBodySet>
#include <cnoid/ValueTree>
#include <cnoid/EigenArchive>
#include <cnoid/EigenUtil>
#include <cnoid/CloneMap>
#include <cnoid/MessageOut>
#include <fmt/format.h>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;


MprTagTraceStatement::MprTagTraceStatement()
    : T_tags(Isometry3::Identity()),
      baseFrameId_(0),
      offsetFrameId_(0),
      targetBodyIndex_(-1)
{
    auto program = lowerLevelProgram();
    program->setLocalPositionListEnabled(true);
    program->setEditable(false);
}


MprTagTraceStatement::MprTagTraceStatement(const MprTagTraceStatement& org, CloneMap* cloneMap)
    : MprStructuredStatement(org, cloneMap),
      tagGroupName_(org.tagGroupName_),
      T_tags(org.T_tags),
      baseFrameId_(org.baseFrameId_),
      offsetFrameId_(org.offsetFrameId_),
      targetBodyIndex_(org.targetBodyIndex_)
{
    if(org.subBodyPositions_){
        subBodyPositions_ = org.subBodyPositions_->clone();
    }
    
    auto program = lowerLevelProgram();
    program->setLocalPositionListEnabled(true);
    program->setEditable(false);
    
    if(org.tagGroup_ && cloneMap){
        tagGroup_ = cloneMap->getClone(org.tagGroup_);
    }
}


std::string MprTagTraceStatement::label(int index) const
{
    if(index == 0){
        return "Trace";
    } else if(index == 1){
        if(!tagGroupName_.empty()){
            return tagGroupName_.c_str();
        } else {
            return "-----";
        }
    }
    return string();
}


void MprTagTraceStatement::setTagGroup
(PositionTagGroup* tags, bool doUpdateTagGroupName, bool doUpdateTagTraceProgram)
{
    if(tags != tagGroup_){
        tagGroupConnections.disconnect();
        tagGroup_ = tags;
        if(doUpdateTagGroupName){
            if(tags){
                tagGroupName_ = tags->name();
            } else {
                tagGroupName_.clear();
            }
        }
        if(tagGroup_){
            connectTagGroupUpdateSignals();
        }
        if(doUpdateTagTraceProgram){
            updateTagTraceProgram();
        }
    }
}


void MprTagTraceStatement::connectTagGroupUpdateSignals()
{
    if(tagGroup_){
        tagGroupConnections.add(
            tagGroup_->sigTagAdded().connect(
                [&](int index){ onTagAdded(index); }));
        tagGroupConnections.add(
            tagGroup_->sigTagRemoved().connect(
                [&](int index, PositionTag*, bool isChangingOrder){
                    onTagRemoved(index, isChangingOrder); }));
        tagGroupConnections.add(
            tagGroup_->sigTagPositionUpdated().connect(
                [&](int index){ onTagPositionUpdated(index); }));
    }
}


void MprTagTraceStatement::updateFramesWithCurrentFrames(BodyKinematicsKit* kinematicsKit)
{
    setBaseFrameId(kinematicsKit->currentBaseFrameId());
    setOffsetFrameId(kinematicsKit->currentOffsetFrameId());
}


void MprTagTraceStatement::updateTagGroupPositionWithGlobalCoordinate
(BodyKinematicsKit* kinematicsKit, const Isometry3& T_global)
{
    auto T_base = kinematicsKit->globalBasePosition(baseFrameId_);
    T_tags = T_base.inverse(Eigen::Isometry) * T_global;
}


bool MprTagTraceStatement::isExpandedByDefault() const
{
    return false;
}


void MprTagTraceStatement::onTagAdded(int /* index */)
{
    updateTagTraceProgram();
}


void MprTagTraceStatement::onTagRemoved(int /* index */, bool /* isChangingOrder */)
{
    //! \todo Skip to update the program when isChangingOrder is true
    updateTagTraceProgram();
}


void MprTagTraceStatement::onTagPositionUpdated(int /* index */)
{
    updateTagTraceProgram();
}


bool MprTagTraceStatement::fetchSubBodyPositions(KinematicBodySet* bodySet)
{
    if(!subBodyPositions_){
        subBodyPositions_ = new MprCompositePosition;
    }
    subBodyPositions_->clearPositions();

    targetBodyIndex_ = bodySet->mainBodyPartIndex();
    for(auto index : bodySet->validBodyPartIndices()){
        if(index != targetBodyIndex_){
            subBodyPositions_->setPosition(index, new MprFkPosition);
        }
    }

    if(subBodyPositions_->empty()){
        subBodyPositions_.reset();
        targetBodyIndex_ = -1;
        return true;
    }

    return subBodyPositions_->fetch(bodySet, MessageOut::interactive());
}


bool MprTagTraceStatement::decomposeIntoTagTraceStatements()
{
    if(!tagGroup_){
        return false;
    }
    auto program = holderProgram();
    if(!program){
        return false;
    }
    auto positions = program->positionList();
    
    if(!updateTagTraceProgram()){
        return false;
    }

    CloneMap cloneMap;
    MprGroupStatementPtr group = new MprGroupStatement;
    group->setGroupName(tagGroup_->name());
    auto decomposed = group->lowerLevelProgram();

    for(auto srcStatement : *lowerLevelProgram()){
        MprStatementPtr statement = srcStatement->clone(cloneMap);
        MprPositionStatementPtr positionStatement = dynamic_pointer_cast<MprPositionStatement>(statement);
        if(positionStatement){
            auto srcPosition = dynamic_pointer_cast<MprPositionStatement>(srcStatement)->position();
            if(!srcPosition){
                return false;
            }
            auto position = srcPosition->clone();
            auto id = positions->createNextId();
            position->setId(id);
            positions->append(position);
            positionStatement->setPositionId(id);
        }
        decomposed->append(statement);
    }

    // Replace the original statement with the group statement that has the decomposed statements.
    auto iter = program->find(this);
    iter = program->remove(iter);
    program->insert(iter, group);

    return true;
}


bool MprTagTraceStatement::read(MprProgram* program, const Mapping* archive)
{
    if(!MprStructuredStatement::read(program, archive)){
        return false;
    }

    tagGroupName_.clear();
    archive->read("tag_group_name", tagGroupName_);

    Vector3 v;
    if(cnoid::read(archive, "translation", v)){
        T_tags.translation() = v;
    } else {
        T_tags.translation().setZero();
    }
    if(cnoid::read(archive, "rpy", v)){
        T_tags.linear() = rotFromRpy(radian(v));
    } else {
        T_tags.linear().setIdentity();
    }
    baseFrameId_.read(archive, "base_frame");
    offsetFrameId_.read(archive, "offset_frame");

    if(tagGroup_ && (tagGroup_->name() != tagGroupName_)){
        setTagGroup(nullptr, false, true);
    }

    targetBodyIndex_ = -1;
    auto node = archive->findMapping("sub_body_positions");
    if(!node->isValid()){
        subBodyPositions_.reset();
    } else {
        subBodyPositions_ = new MprCompositePosition;
        if(!subBodyPositions_->read(node)){
            return false;
        }
        archive->read("target_body_index", targetBodyIndex_);
    }
        
    notifyUpdate();

    return true;
}


bool MprTagTraceStatement::write(Mapping* archive) const
{
    if(!MprStructuredStatement::write(archive)){
        return false;
    }
    
    if(tagGroup_){
        if(!tagGroupName_.empty()){
            archive->write("tag_group_name", tagGroupName_);
        }
    }
    archive->setFloatingNumberFormat("%.10g");
    cnoid::write(archive, "translation", Vector3(T_tags.translation()));
    cnoid::write(archive, "rpy", degree(rpyFromRot(T_tags.linear())));
    baseFrameId_.write(archive, "base_frame");
    offsetFrameId_.write(archive, "offset_frame");

    if(subBodyPositions_){
        if(targetBodyIndex_ < 0){
            return false;
        }
        archive->write("target_body_index", targetBodyIndex_);
        if(!subBodyPositions_->write(archive->createMapping("sub_body_positions"))){
            return false;
        }
    }
    return true;
}


namespace {

struct StatementTypeRegistration {
    StatementTypeRegistration(){
        MprStatementRegistration()
            .registerAbstractType<MprTagTraceStatement, MprStatement>();
    }
} registration;

}
