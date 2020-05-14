#include "CoordinateFrameItem.h"
#include "CoordinateFrameListItem.h"
#include "ItemManager.h"
#include "PutPropertyFunction.h"
#include "Archive.h"
#include <cnoid/CoordinateFrame>
#include <cnoid/CoordinateFrameList>
#include <fmt/format.h>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;

namespace cnoid {

class CoordinateFrameItem::Impl
{
public:
    GeneralId frameId;
    CoordinateFrameListItem* frameListItem;
    CoordinateFrameList* frameList;
    Signal<void()> sigLocationChanged;
    ScopedConnection frameConnection;
    bool isChangingCheckStatePassively;
    
    Impl(CoordinateFrameItem* self);
    Impl(CoordinateFrameItem* self, const Impl& org);
    void initialize(CoordinateFrameItem* self);
    bool setFrameId(const GeneralId& id);
    void onCheckToggled(bool on);
    void putFrameAttributes(PutPropertyFunction& putProperty);
};

}


void CoordinateFrameItem::initializeClass(ExtensionManager* ext)
{
    auto& im = ext->itemManager();
    im.registerClass<CoordinateFrameItem>(N_("CoordinateFrameItem"));
}


CoordinateFrameItem::CoordinateFrameItem()
{
    impl = new Impl(this);
}


CoordinateFrameItem::Impl::Impl(CoordinateFrameItem* self)
{
    initialize(self);
}


CoordinateFrameItem::CoordinateFrameItem(const CoordinateFrameItem& org)
    : Item(org)
{
    impl = new Impl(this, *org.impl);
}


CoordinateFrameItem::Impl::Impl(CoordinateFrameItem* self, const Impl& org)
    : frameId(org.frameId)
{
    initialize(self);
}


void CoordinateFrameItem::Impl::initialize(CoordinateFrameItem* self)
{
    frameListItem = nullptr;
    frameList = nullptr;
    self->sigCheckToggled().connect([&](bool on){ onCheckToggled(on); });
    isChangingCheckStatePassively = false;
}


CoordinateFrameItem::~CoordinateFrameItem()
{
    delete impl;
}


Item* CoordinateFrameItem::doDuplicate() const
{
    return new CoordinateFrameItem(*this);
}


void CoordinateFrameItem::onPositionChanged()
{
    impl->frameListItem = dynamic_cast<CoordinateFrameListItem*>(parentItem());
    if(impl->frameListItem){
        impl->frameList = impl->frameListItem->frameList();
    } else {
        impl->frameList = nullptr;
    }
}    


std::string CoordinateFrameItem::displayName() const
{
    if(impl->frameListItem){
        return impl->frameListItem->getFrameItemDisplayName(this);
    }
    return name();
}


bool CoordinateFrameItem::setFrameId(const GeneralId& id)
{
    return impl->setFrameId(id);
}


bool CoordinateFrameItem::Impl::setFrameId(const GeneralId& id)
{
    bool updated = true;
    if(frameList){
        if(auto frame = frameList->findFrame(frameId)){
            if(frameList->resetId(frame, id)){
                frame->notifyAttributeChange();
            } else {
                updated = false;
            }
        }
    }
    if(updated){
        frameId = id;
    }
    return updated;
}


const GeneralId& CoordinateFrameItem::frameId() const
{
    return impl->frameId;
}


CoordinateFrameList* CoordinateFrameItem::frameList()
{
    return impl->frameList;
}


const CoordinateFrameList* CoordinateFrameItem::frameList() const
{
    return impl->frameList;
}


CoordinateFrame* CoordinateFrameItem::frame()
{
    if(impl->frameList){
        return impl->frameList->findFrame(impl->frameId);
    }
    return nullptr;
}


const CoordinateFrame* CoordinateFrameItem::frame() const
{
    return const_cast<CoordinateFrameItem*>(this)->frame();
}


bool CoordinateFrameItem::isBaseFrame() const
{
    if(impl->frameList){
        return impl->frameList->isForBaseFrames();
    }
    return false;
}


bool CoordinateFrameItem::isOffsetFrame() const
{
    if(impl->frameList){
        return impl->frameList->isForOffsetFrames();
    }
    return false;
}


void CoordinateFrameItem::Impl::onCheckToggled(bool on)
{
    if(!isChangingCheckStatePassively && frameListItem){
        frameListItem->setFrameMarkerVisible(frameId, on);
    }
}


void CoordinateFrameItem::setVisibilityCheck(bool on)
{
    impl->isChangingCheckStatePassively = true;
    setChecked(on);
    impl->isChangingCheckStatePassively = false;
}


int CoordinateFrameItem::getLocationType() const
{
    if(impl->frameList){
        if(impl->frameList->isForBaseFrames()){
            return ParentRelativeLocation;
        } else if(impl->frameList->isForOffsetFrames()){
            return OffsetLocation;
        }
    }
    return InvalidLocation;
}


LocatableItem* CoordinateFrameItem::getParentLocatableItem()
{
    if(impl->frameListItem){
        return impl->frameListItem->getParentLocatableItem();
    }
    return nullptr;
}


std::string CoordinateFrameItem::getLocationName() const
{
    auto frame_ = frame();
    if(!impl->frameListItem || !frame_){
        return displayName();
    } else {
        auto listName = impl->frameListItem->displayName();
        auto id = frame_->id().label();
        auto note = frame_->note();
        if(auto parent = impl->frameListItem->getParentLocatableItem()){
            auto parentName = parent->getLocationName();
            if(note.empty()){
                return format("{0} {1} {2}", parentName, listName, id);
            } else {
                return format("{0} {1} {2} ( {3} )", parentName, listName, id, note);
            }
        } else {
            if(note.empty()){
                return format("{0} {1}", listName, id);
            } else {
                return format("{0} {1} ( {2} )", listName, id, note);
            }
        }
    }
}


Position CoordinateFrameItem::getLocation() const
{
    if(auto frame_ = frame()){
        return frame_->position();
    }
    return Position::Identity();
}


void CoordinateFrameItem::setLocation(const Position& T)
{
    if(auto frame_ = frame()){
        frame_->setPosition(T);
        frame_->notifyPositionChange();
    }
}


SignalProxy<void()> CoordinateFrameItem::sigLocationChanged()
{
    if(auto frame_ = frame()){
        impl->frameConnection =
            frame_->sigPositionChanged().connect([&](){ impl->sigLocationChanged(); });
    }
    return impl->sigLocationChanged;
}


void CoordinateFrameItem::doPutProperties(PutPropertyFunction& putProperty)
{
    impl->putFrameAttributes(putProperty);
}


void CoordinateFrameItem::putFrameAttributes(PutPropertyFunction& putProperty)
{
    impl->putFrameAttributes(putProperty);
}


void CoordinateFrameItem::Impl::putFrameAttributes(PutPropertyFunction& putProperty)
{
    auto& id = frameId;
    CoordinateFrame* frame = nullptr;
    bool isDefaultFrame = false;
    if(frameList){
        frame = frameList->findFrame(frameId);
        if(frame && frameList->isDefaultFrame(frame)){
            isDefaultFrame = true;
        }
    }
    if(isDefaultFrame){ // Read only
        putProperty(_("ID"), id.label());
        if(frame){
            putProperty(_("Note"), frame->note());
            putProperty(_("Global"), frame->isGlobal());
        }
    } else {
        if(id.isInt()){
            putProperty(_("ID"), id.toInt(),
                        [&](int value){ return setFrameId(value); });
        } else if(id.isString()){
            putProperty(_("ID"), id.toString(),
                        [&](const string& value){ return setFrameId(value); });
        }
        if(frame){
            putProperty(_("Note"), frame->note(),
                        [frame](const string& text){
                            frame->setNote(text);
                            frame->notifyAttributeChange();
                            return true;
                        });
            putProperty(_("Global"), frame->isGlobal(),
                        [frame](bool on){
                            frame->setGlobal(on);
                            frame->notifyAttributeChange();
                            return true;
                        });
        }
    }
}


bool CoordinateFrameItem::store(Archive& archive)
{
    if(auto list = frameList()){
        if(auto frame = list->findFrame(impl->frameId)){
            return frame->write(archive);
        }
    }
    return false;
}
    

bool CoordinateFrameItem::restore(const Archive& archive)
{
    auto frameListItem = dynamic_cast<CoordinateFrameListItem*>(archive.currentParentItem());
    if(frameListItem){
        CoordinateFramePtr frame = new CoordinateFrame;
        if(frame->read(archive)){
            impl->frameId = frame->id();
            setName(impl->frameId.label());
            return frameListItem->frameList()->append(frame);
        }
    }
    return false;
}
