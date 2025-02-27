#ifndef CNOID_BODY_PLUGIN_AIST_SIMULATOR_ITEM_H
#define CNOID_BODY_PLUGIN_AIST_SIMULATOR_ITEM_H

#include "SimulatorItem.h"
#include <cnoid/Collision>
#include "exportdecl.h"

namespace cnoid {

class ContactMaterial;
class ExtraJoint;
        
class CNOID_EXPORT AISTSimulatorItem : public SimulatorItem
{
public:
    static void initializeClass(ExtensionManager* ext);
        
    AISTSimulatorItem();
    virtual ~AISTSimulatorItem();

    enum DynamicsMode {
        ForwardDynamicsMode,
        KinematicsMode,
        FORWARD_DYNAMICS = ForwardDynamicsMode,
        KINEMATICS = KinematicsMode
    };

    enum IntegrationMode {
        SemiImplicitEuler,
        RungeKutta,
        EULER_INTEGRATION = SemiImplicitEuler,
        RUNGE_KUTTA_INTEGRATION = RungeKutta
    };

    void setDynamicsMode(int mode);
    void setIntegrationMode(int mode);
    void setGravity(const Vector3& gravity);
    const Vector3& gravity() const;
    void setFrictionCoefficientRange(double minFriction, double maxFriction);
    double minFrictionCoefficient() const;
    double maxFrictionCoefficient() const;
    void setContactCullingDistance(double value);        
    void setContactCullingDepth(double value);        
    void setErrorCriterion(double value);        
    void setMaxNumIterations(int value);
    void setContactCorrectionDepth(double value);
    void setContactCorrectionVelocityRatio(double value);
    void setEpsilon(double epsilon);
    void set2Dmode(bool on);
    void setKinematicWalkingEnabled(bool on);

    [[deprecated("This function does nothing. Set Link::LinkContactState to Link::sensingMode from a controller.")]]
    void setConstraintForceOutputEnabled(bool on);

    void addExtraJoint(ExtraJoint& extraJoint);
    void clearExtraJoints();

    virtual Vector3 getGravity() const override;
    virtual void setForcedPosition(BodyItem* bodyItem, const Isometry3& T) override;
    virtual bool isForcedPositionActiveFor(BodyItem* bodyItem) const override;
    virtual void clearForcedPositions() override;

    typedef std::function<bool(Link* link1, Link* link2,
                               const std::vector<Collision>& collisions,
                               ContactMaterial* contactMaterial)> CollisionHandler;
    
    void registerCollisionHandler(const std::string& name, CollisionHandler handler);
    bool unregisterCollisionHandler(const std::string& name);

    //[[deprecated]]
    void setFriction(Link* link1, Link* link2, double staticFriction, double dynamicFriction);

protected:
    AISTSimulatorItem(const AISTSimulatorItem& org);
    virtual void clearSimulation() override;
    virtual SimulationBody* createSimulationBody(Body* orgBody, CloneMap& cloneMap) override;
    virtual bool initializeSimulation(const std::vector<SimulationBody*>& simBodies) override;
    virtual bool completeInitializationOfSimulation() override;
    virtual bool stepSimulation(const std::vector<SimulationBody*>& activeSimBodies) override;
    virtual void finalizeSimulation() override;
    virtual std::shared_ptr<CollisionLinkPairList> getCollisions() override;
        
    virtual Item* doCloneItem(CloneMap* cloneMap) const override;
    virtual void doPutProperties(PutPropertyFunction& putProperty) override;
    virtual bool store(Archive& archive) override;
    virtual bool restore(const Archive& archive) override;

private:
    class Impl;
    Impl* impl;
};

typedef ref_ptr<AISTSimulatorItem> AISTSimulatorItemPtr;

}

#endif
