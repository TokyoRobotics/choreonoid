#ifndef CNOID_UTIL_COORDINATE_FRAME_H
#define CNOID_UTIL_COORDINATE_FRAME_H

#include "GeneralId.h"
#include "Referenced.h"
#include <cnoid/EigenTypes>
#include <cnoid/Signal>
#include <string>
#include "exportdecl.h"

namespace cnoid {

class CoordinateFrameList;
class Mapping;

class CNOID_EXPORT CoordinateFrame : public Referenced
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    CoordinateFrame();
    CoordinateFrame(const GeneralId& id);
    CoordinateFrame(const CoordinateFrame& org);

    /**
       This constructor is used in a special case where the frame is not actually
       contained in the owner, but the frame needs to set the owner formally.
    */
    CoordinateFrame(const GeneralId& id, CoordinateFrameList* owner);

    virtual CoordinateFrame* clone() const;

    const GeneralId& id() const { return id_; }

    void setGlobal(bool on) { isGlobal_ = on; }
    bool isGlobal() const { return isGlobal_; }

    const Position& T() const { return T_; }
    const Position& position() const { return T_; }
    void setPosition(const Position& T) { T_ = T; }

    const std::string& note() const { return note_; }
    void setNote(const std::string& note) { note_ = note; }

    CoordinateFrameList* ownerFrameList() const;

    bool read(const Mapping& archive);
    bool write(Mapping& archive) const;

    SignalProxy<void()> sigAttributeChanged();
    SignalProxy<void()> sigPositionChanged();
    void notifyAttributeChange();
    void notifyPositionChange();

private:
    Position T_;
    GeneralId id_;
    bool isGlobal_;
    std::string note_;
    weak_ref_ptr<CoordinateFrameList> ownerFrameList_;
    Signal<void()> sigAttributeChanged_;
    Signal<void()> sigPositionChanged_;

    friend class CoordinateFrameList;
};

typedef ref_ptr<CoordinateFrame> CoordinateFramePtr;

}

#endif
