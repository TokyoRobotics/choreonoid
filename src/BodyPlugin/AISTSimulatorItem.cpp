#include "AISTSimulatorItem.h"
#include "WorldItem.h"
#include "BodyItem.h"
#include "ControllerItem.h"
#include <cnoid/ItemManager>
#include <cnoid/MessageView>
#include <cnoid/PutPropertyFunction>
#include <cnoid/Archive>
#include <cnoid/DyWorld>
#include <cnoid/DyBody>
#include <cnoid/ForwardDynamicsCBM>
#include <cnoid/ConstraintForceSolver>
#include <cnoid/LeggedBodyHelper>
#include <cnoid/CloneMap>
#include <cnoid/FloatingNumberString>
#include <cnoid/EigenUtil>
#include <cnoid/EigenArchive>
#include <cnoid/IdPair>
#include <fmt/format.h>
#include <mutex>
#include <iomanip>
#include <fstream>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;

// for Windows
#undef min
#undef max

namespace {

const bool TRACE_FUNCTIONS = false;
const bool ENABLE_DEBUG_OUTPUT = false;
const double DEFAULT_GRAVITY_ACCELERATION = 9.80665;

class AISTSimBody : public SimulationBody
{
public:
    AISTSimBody(DyBody* body) : SimulationBody(body) { }
};
    

class KinematicWalkBody : public AISTSimBody
{
public:
    KinematicWalkBody(DyBody* body, LeggedBodyHelper* legged)
        : AISTSimBody(body),
          legged(legged) {
        supportFootIndex = 0;
        for(int i=1; i < legged->numFeet(); ++i){
            if(legged->footLink(i)->p().z() < legged->footLink(supportFootIndex)->p().z()){
                supportFootIndex = i;
            }
        }
        traverse.find(legged->footLink(supportFootIndex), true, true);
    }
    LeggedBodyHelper* legged;
    int supportFootIndex;
    LinkTraverse traverse;
};

}


namespace cnoid {
  
class AISTSimulatorItem::Impl
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

    AISTSimulatorItem* self;

    DyWorld<ConstraintForceSolver> world;
    vector<DyLink*> internalStateUpdateLinks;
        
    Selection dynamicsMode;
    Selection integrationMode;
    Vector3 gravity;
    double minFrictionCoefficient;
    double maxFrictionCoefficient;
    FloatingNumberString contactCullingDistance;
    FloatingNumberString contactCullingDepth;
    FloatingNumberString errorCriterion;
    int maxNumIterations;
    FloatingNumberString contactCorrectionDepth;
    FloatingNumberString contactCorrectionVelocityRatio;
    double epsilon;
    bool is2Dmode;
    bool isKinematicWalkingEnabled;
    bool isOldAccelSensorMode;
    bool hasNonRootFreeJoints;

    stdx::optional<int> forcedBodyPositionFunctionId;
    std::mutex forcedBodyPositionMutex;
    DyBody* forcedPositionBody;
    Isometry3 forcedBodyPosition;

    MessageView* mv;

    Impl(AISTSimulatorItem* self);
    Impl(AISTSimulatorItem* self, const Impl& org);
    bool initializeSimulation(const std::vector<SimulationBody*>& simBodies);
    void addBody(AISTSimBody* simBody);
    void clearExternalForces();
    void stepKinematicsSimulation(const std::vector<SimulationBody*>& activeSimBodies);
    void setForcedPosition(BodyItem* bodyItem, const Isometry3& T);
    void doSetForcedPosition();
    void doPutProperties(PutPropertyFunction& putProperty);
    bool store(Archive& archive);
    bool restore(const Archive& archive);

    // for debug
    ofstream os;
};

}


void AISTSimulatorItem::initializeClass(ExtensionManager* ext)
{
    ext->itemManager().registerClass<AISTSimulatorItem, SimulatorItem>(N_("AISTSimulatorItem"));
    ext->itemManager().addCreationPanel<AISTSimulatorItem>();
}


AISTSimulatorItem::AISTSimulatorItem()
    : SimulatorItem("AISTSimulator")
{
    impl = new Impl(this);
    setAllLinkPositionOutputMode(false);
}


AISTSimulatorItem::Impl::Impl(AISTSimulatorItem* self)
    : self(self),
      dynamicsMode(2, CNOID_GETTEXT_DOMAIN_NAME),
      integrationMode(2, CNOID_GETTEXT_DOMAIN_NAME)
{
    dynamicsMode.setSymbol(ForwardDynamicsMode, N_("Forward dynamics"));
    dynamicsMode.setSymbol(KinematicsMode,      N_("Kinematics"));

    integrationMode.setSymbol(SemiImplicitEuler, N_("Semi-implicit Euler"));
    integrationMode.setSymbol(RungeKutta,        N_("Runge-Kutta"));
    integrationMode.select(SemiImplicitEuler);
    
    gravity << 0.0, 0.0, -DEFAULT_GRAVITY_ACCELERATION;

    ConstraintForceSolver& cfs = world.constraintForceSolver;
    minFrictionCoefficient = cfs.minFrictionCoefficient();
    maxFrictionCoefficient = cfs.maxFrictionCoefficient();
    contactCullingDistance = cfs.contactCullingDistance();
    contactCullingDepth = cfs.contactCullingDepth();
    epsilon = cfs.coefficientOfRestitution();
    
    errorCriterion = cfs.gaussSeidelErrorCriterion();
    maxNumIterations = cfs.gaussSeidelMaxNumIterations();
    contactCorrectionDepth = cfs.contactCorrectionDepth();
    contactCorrectionVelocityRatio = cfs.contactCorrectionVelocityRatio();

    isKinematicWalkingEnabled = false;
    is2Dmode = false;
    isOldAccelSensorMode = false;
    hasNonRootFreeJoints = false;

    mv = MessageView::instance();
}


AISTSimulatorItem::AISTSimulatorItem(const AISTSimulatorItem& org)
    : SimulatorItem(org),
      impl(new Impl(this, *org.impl))
{

}


AISTSimulatorItem::Impl::Impl(AISTSimulatorItem* self, const Impl& org)
    : self(self),
      dynamicsMode(org.dynamicsMode),
      integrationMode(org.integrationMode)
{
    gravity = org.gravity;
    minFrictionCoefficient = org.minFrictionCoefficient;
    maxFrictionCoefficient = org.maxFrictionCoefficient;
    contactCullingDistance = org.contactCullingDistance;
    contactCullingDepth = org.contactCullingDepth;
    errorCriterion = org.errorCriterion;
    maxNumIterations = org.maxNumIterations;
    contactCorrectionDepth = org.contactCorrectionDepth;
    contactCorrectionVelocityRatio = org.contactCorrectionVelocityRatio;
    epsilon = org.epsilon;
    isKinematicWalkingEnabled = org.isKinematicWalkingEnabled;
    is2Dmode = org.is2Dmode;
    isOldAccelSensorMode = org.isOldAccelSensorMode;

    mv = MessageView::instance();
}


AISTSimulatorItem::~AISTSimulatorItem()
{
    delete impl;
}


void AISTSimulatorItem::setDynamicsMode(int mode)
{
    impl->dynamicsMode.select(mode);
}


void AISTSimulatorItem::setIntegrationMode(int mode)
{
    impl->integrationMode.select(mode);
}


void AISTSimulatorItem::setGravity(const Vector3& gravity)
{
    impl->gravity = gravity;
}


const Vector3& AISTSimulatorItem::gravity() const
{
    return impl->gravity;
}


void AISTSimulatorItem::setFrictionCoefficientRange(double minFriction, double maxFriction)
{
    impl->minFrictionCoefficient = minFriction;
    impl->maxFrictionCoefficient = maxFriction;
}


double AISTSimulatorItem::minFrictionCoefficient() const
{
    return impl->minFrictionCoefficient;
}


double AISTSimulatorItem::maxFrictionCoefficient() const
{
    return impl->maxFrictionCoefficient;
}


void AISTSimulatorItem::setFriction(Link* link1, Link* link2, double staticFriction, double dynamicFriction)
{
    impl->mv->putln(
        _("AISTSimulatorItem::setFriction(Link* link1, Link* link2, double staticFriction, double dynamicFriction) "
          "is not supported in this version.\n"
          "Please use the material table instead of it."),
        MessageView::Warning);
}


void AISTSimulatorItem::registerCollisionHandler(const std::string& name, CollisionHandler handler)
{
    impl->world.constraintForceSolver.registerCollisionHandler(name, handler);
}


bool AISTSimulatorItem::unregisterCollisionHandler(const std::string& name)
{
    return impl->world.constraintForceSolver.unregisterCollisionHandler(name);
}


void AISTSimulatorItem::setContactCullingDistance(double value)    
{
    impl->contactCullingDistance = value;
}


void AISTSimulatorItem::setContactCullingDepth(double value)    
{
    impl->contactCullingDepth = value;
}

    
void AISTSimulatorItem::setErrorCriterion(double value)    
{
    impl->errorCriterion = value;
}

    
void AISTSimulatorItem::setMaxNumIterations(int value)
{
    impl->maxNumIterations = value;   
}


void AISTSimulatorItem::setContactCorrectionDepth(double value)
{
    impl->contactCorrectionDepth = value;
}


void AISTSimulatorItem::setContactCorrectionVelocityRatio(double value)
{
    impl->contactCorrectionVelocityRatio = value;
}


void AISTSimulatorItem::setEpsilon(double epsilon)
{
    impl->epsilon = epsilon;
}


void AISTSimulatorItem::set2Dmode(bool on)
{
    impl->is2Dmode = on;
}


void AISTSimulatorItem::setKinematicWalkingEnabled(bool on)
{
    impl->isKinematicWalkingEnabled = on;
}


void AISTSimulatorItem::setConstraintForceOutputEnabled(bool /* on */)
{

}


void AISTSimulatorItem::addExtraJoint(ExtraJoint& extraJoint)
{
    impl->world.addExtraJoint(extraJoint);
}


void AISTSimulatorItem::clearExtraJoints()
{
    impl->world.clearExtraJoints();
}


Item* AISTSimulatorItem::doCloneItem(CloneMap* /* cloneMap */) const
{
    return new AISTSimulatorItem(*this);
}


void AISTSimulatorItem::clearSimulation()
{
    impl->world.clearBodies();
    impl->internalStateUpdateLinks.clear();
}


SimulationBody* AISTSimulatorItem::createSimulationBody(Body* orgBody, CloneMap& cloneMap)
{
    if(!orgBody->isStaticModel() && orgBody->mass() <= 0.0){
        impl->mv->putln(
            format(_("The mass of {0} is {1}, which cannot be simulated by AISTSimulatorItem."),
                   orgBody->name(), orgBody->mass()),
            MessageView::Error);
        return nullptr;
    }
        
    if(orgBody->parentBody()){
        impl->mv->putln(
            format(_("{0} is attached to {1}, but attached bodies are not supported by AISTSimulatorItem."),
                   orgBody->name(), orgBody->parentBody()->name()),
            MessageView::Error);
        return nullptr;
    }
    
    SimulationBody* simBody = nullptr;
    DyBody* body = new DyBody;
    cloneMap.setClone(orgBody, body);
    body->copyFrom(orgBody, &cloneMap);

    if(impl->dynamicsMode.is(KinematicsMode) && impl->isKinematicWalkingEnabled){
        LeggedBodyHelper* legged = getLeggedBodyHelper(body);
        if(legged->isValid()){
            simBody = new KinematicWalkBody(body, legged);
        }
    }
    if(!simBody){
        simBody = new AISTSimBody(body);
    }

    return simBody;
}


bool AISTSimulatorItem::initializeSimulation(const std::vector<SimulationBody*>& simBodies)
{
    return impl->initializeSimulation(simBodies);
}


bool AISTSimulatorItem::Impl::initializeSimulation(const std::vector<SimulationBody*>& simBodies)
{
    if(ENABLE_DEBUG_OUTPUT){
        static int ntest = 0;
        os.open((string("test-log-") + std::to_string(ntest++) + ".log").c_str());
        os << setprecision(30);
    }

    if(integrationMode.is(SemiImplicitEuler)){
        world.setEulerMethod();
    } else if(integrationMode.is(RungeKutta)){
        world.setRungeKuttaMethod();
    }
    world.setGravityAcceleration(gravity);
    world.enableSensors(true);
    world.setOldAccelSensorCalcMode(isOldAccelSensorMode);
    world.setTimeStep(self->worldTimeStep());
    world.setCurrentTime(0.0);

    ConstraintForceSolver& cfs = world.constraintForceSolver;
    cfs.setMaterialTable(self->worldItem()->materialTable());
    cfs.setGaussSeidelErrorCriterion(errorCriterion.value());
    cfs.setGaussSeidelMaxNumIterations(maxNumIterations);
    cfs.setContactDepthCorrection(contactCorrectionDepth.value(), contactCorrectionVelocityRatio.value());
    
    self->addPostDynamicsFunction([&](){ clearExternalForces(); });

    hasNonRootFreeJoints = false;
    for(size_t i=0; i < simBodies.size(); ++i){
        addBody(static_cast<AISTSimBody*>(simBodies[i]));
    }
    if(hasNonRootFreeJoints && !self->isAllLinkPositionOutputMode()){
        bool confirmed = showConfirmDialog(
            _("Confirmation of all link position recording mode"),
            format(_("{0}: There is a model that has free-type joints other than the root link "
                     "and all the link positions should be recorded in this case. "
                     "Do you enable the mode to do it?"), self->displayName()));
        if(confirmed){
            self->setAllLinkPositionOutputMode(true);
            self->notifyUpdate();
        }
    }

    if(world.hasHighGainDynamics()){
        mv->putln(format(_("{} uses the ForwardDynamicsCBM module to perform the high-gain control."),
                         self->displayName()));
    }

    cfs.setFrictionCoefficientRange(minFrictionCoefficient, maxFrictionCoefficient);
    cfs.setContactCullingDistance(contactCullingDistance.value());
    cfs.setContactCullingDepth(contactCullingDepth.value());
    cfs.setCoefficientOfRestitution(epsilon);
    cfs.setCollisionDetector(self->getOrCreateCollisionDetector());

    if(is2Dmode){
        cfs.set2Dmode(true);
    }

    return true;
}


void AISTSimulatorItem::Impl::addBody(AISTSimBody* simBody)
{
    DyBody* body = static_cast<DyBody*>(simBody->body());

    //! \todo Move the following process to DySubBody's constructor
    for(auto& link : body->links()){
        if(link->isFreeJoint() && !link->isRoot()){
            hasNonRootFreeJoints = true;
        }
        int mode = link->actuationMode() & ~Link::LinkExtWrench;
        if(mode){
            if(mode == Link::JointEffort){
                continue;
            }
            if(link->jointType() == Link::PseudoContinuousTrackJoint){
                if(mode == Link::JointVelocity || mode == Link::DeprecatedJointSurfaceVelocity){
                    continue;
                } else {
                    mv->putln(
                        format(_("{0}: Actuation mode \"{1}\" cannot be used for the pseudo continuous track link {2} of {3}"),
                               self->displayName(), Link::getStateModeString(mode), link->name(), body->name()),
                        MessageView::Warning);
                }
            }
            if(mode == Link::JointDisplacement || mode == Link::JointVelocity || mode == Link::LinkPosition){
                continue;

            } else if(link->actuationMode() == Link::AllStateHighGainActuationMode){
                internalStateUpdateLinks.push_back(link);

            } else if(mode == Link::DeprecatedJointSurfaceVelocity){
                if(link->isFixedJoint()){
                    link->setJointType(Link::PseudoContinuousTrackJoint);
                }
            } else {
                mv->putln(
                    format(_("{0}: Actuation mode \"{1}\" specified for the {2} link of {3} is not supported."),
                           self->displayName(), Link::getStateModeString(mode), link->name(), body->name()),
                    MessageView::Warning);
            }
        }
    }

    int bodyIndex = world.addBody(body);

    auto bodyItem = simBody->bodyItem();
    world.constraintForceSolver.setBodyCollisionDetectionMode(
        bodyIndex, bodyItem->isCollisionDetectionEnabled(), bodyItem->isSelfCollisionDetectionEnabled());
}


bool AISTSimulatorItem::completeInitializationOfSimulation()
{
    impl->world.initialize();
    return true;
}


void AISTSimulatorItem::Impl::clearExternalForces()
{
    world.constraintForceSolver.clearExternalForces();
}


bool AISTSimulatorItem::stepSimulation(const std::vector<SimulationBody*>& activeSimBodies)
{
    switch(impl->dynamicsMode.which()){

    case ForwardDynamicsMode:
    {
        // Update the internal states for a special actuation mode
        bool doRefresh = false;
        for(auto& dyLink : impl->internalStateUpdateLinks){
            if(dyLink->actuationMode() == Link::AllStateHighGainActuationMode){
                if(dyLink->hasActualJoint()){
                    dyLink->q() = dyLink->q_target();
                    dyLink->dq() = dyLink->dq_target();
                }
                dyLink->vo() = dyLink->v() - dyLink->w().cross(dyLink->p());
                doRefresh = true;
            }
        }
        if(doRefresh){
            impl->world.refreshState();
        }
        impl->world.calcNextState();
        break;
    }
        
    case KinematicsMode:
        impl->stepKinematicsSimulation(activeSimBodies);
        break;
    }
    return true;
}


void AISTSimulatorItem::Impl::stepKinematicsSimulation(const std::vector<SimulationBody*>& activeSimBodies)
{
    for(size_t i=0; i < activeSimBodies.size(); ++i){
        SimulationBody* simBody = activeSimBodies[i];
        Body* body = simBody->body();
        bool hasJointAngleActuation = false;
        for(auto& joint : body->allJoints()){
            if(joint->actuationMode() == Link::JointDisplacement){
                joint->q() = joint->q_target();
                joint->dq() = joint->dq_target();
                hasJointAngleActuation = true;
            }
        }
        
        if(!isKinematicWalkingEnabled){
            if(hasJointAngleActuation){
                body->calcForwardKinematics(true, true);
            }
        } else {
            KinematicWalkBody* walkBody = dynamic_cast<KinematicWalkBody*>(simBody);
            if(!walkBody){
                body->calcForwardKinematics(true, true);
            } else {
                walkBody->traverse.calcForwardKinematics(true, true);
                
                LeggedBodyHelper* legged = walkBody->legged;
                const int supportFootIndex = walkBody->supportFootIndex;
                int nextSupportFootIndex = supportFootIndex;
                Link* supportFoot = legged->footLink(supportFootIndex);
                Link* nextSupportFoot = supportFoot;
                const int n = legged->numFeet();
                for(int i=0; i < n; ++i){
                    if(i != supportFootIndex){
                        Link* foot = legged->footLink(i);
                        if(foot->p().z() < nextSupportFoot->p().z()){
                            nextSupportFootIndex = i;
                            nextSupportFoot = foot;
                        }
                    }
                }
                if(nextSupportFoot != supportFoot){
                    nextSupportFoot->p().z() = supportFoot->p().z();
                    walkBody->supportFootIndex = nextSupportFootIndex;
                    supportFoot = nextSupportFoot;
                    walkBody->traverse.find(supportFoot, true, true);
                    walkBody->traverse.calcForwardKinematics(true, true);
                }
            }
        }
    }
}


void AISTSimulatorItem::finalizeSimulation()
{
    if(ENABLE_DEBUG_OUTPUT){
        impl->os.close();
    }
}


std::shared_ptr<CollisionLinkPairList> AISTSimulatorItem::getCollisions()
{
    return impl->world.constraintForceSolver.getCollisions();
}


Vector3 AISTSimulatorItem::getGravity() const
{
    return impl->gravity;
}


void AISTSimulatorItem::setForcedPosition(BodyItem* bodyItem, const Isometry3& T)
{
    impl->setForcedPosition(bodyItem, T);
}


void AISTSimulatorItem::Impl::setForcedPosition(BodyItem* bodyItem, const Isometry3& T)
{
    if(SimulationBody* simBody = self->findSimulationBody(bodyItem)){
        {
            std::lock_guard<std::mutex> lock(forcedBodyPositionMutex);
            forcedPositionBody = static_cast<DyBody*>(simBody->body());
            forcedBodyPosition = T;
        }
        if(!forcedBodyPositionFunctionId){
            forcedBodyPositionFunctionId =
                self->addPostDynamicsFunction([&](){ doSetForcedPosition(); });
        }
    }
}


bool AISTSimulatorItem::isForcedPositionActiveFor(BodyItem* bodyItem) const
{
    bool isActive = false;
    if(impl->forcedBodyPositionFunctionId){
        SimulationBody* simBody = const_cast<AISTSimulatorItem*>(this)->findSimulationBody(bodyItem);
        {
            std::lock_guard<std::mutex> lock(impl->forcedBodyPositionMutex);
            if(impl->forcedPositionBody == static_cast<DyBody*>(simBody->body())){
                isActive = true;
            }
        }
    }
    return isActive;
}


void AISTSimulatorItem::clearForcedPositions()
{
    if(impl->forcedBodyPositionFunctionId){
        removePostDynamicsFunction(*impl->forcedBodyPositionFunctionId);
        impl->forcedBodyPositionFunctionId = stdx::nullopt;
    }
}
    

void AISTSimulatorItem::Impl::doSetForcedPosition()
{
    std::lock_guard<std::mutex> lock(forcedBodyPositionMutex);
    DyLink* rootLink = forcedPositionBody->rootLink();
    rootLink->setPosition(forcedBodyPosition);
    rootLink->v().setZero();
    rootLink->w().setZero();
    rootLink->vo().setZero();
    forcedPositionBody->calcSpatialForwardKinematics();
}


void AISTSimulatorItem::doPutProperties(PutPropertyFunction& putProperty)
{
    SimulatorItem::doPutProperties(putProperty);
    impl->doPutProperties(putProperty);
}


void AISTSimulatorItem::Impl::doPutProperties(PutPropertyFunction& putProperty)
{
    putProperty(_("Dynamics mode"), dynamicsMode,
                [&](int index){ return dynamicsMode.selectIndex(index); });
    putProperty(_("Integration mode"), integrationMode,
                [&](int index){ return integrationMode.selectIndex(index); });
    putProperty(_("Gravity"), str(gravity), [&](const string& v){ return toVector3(v, gravity); });

    putProperty.reset().decimals(1).min(0.0);
    putProperty(_("Min friction coefficient"), minFrictionCoefficient, changeProperty(minFrictionCoefficient));
    putProperty(_("Max friction coefficient"), maxFrictionCoefficient, changeProperty(maxFrictionCoefficient));
                                           
    putProperty(_("Contact culling distance"), contactCullingDistance,
                [&](const string& v){ return contactCullingDistance.setNonNegativeValue(v); });
    putProperty(_("Contact culling depth"), contactCullingDepth,
                [&](const string& v){ return contactCullingDepth.setNonNegativeValue(v); });
    putProperty(_("Error criterion"), errorCriterion,
                [&](const string& v){ return errorCriterion.setPositiveValue(v); });
    putProperty.min(1)(_("Max iterations"), maxNumIterations, changeProperty(maxNumIterations));
    putProperty(_("CC depth"), contactCorrectionDepth,
                [&](const string& v){ return contactCorrectionDepth.setNonNegativeValue(v); });
    putProperty(_("CC v-ratio"), contactCorrectionVelocityRatio,
                [&](const string& v){ return contactCorrectionVelocityRatio.setNonNegativeValue(v); });
    putProperty(_("Kinematic walking"), isKinematicWalkingEnabled,
                changeProperty(isKinematicWalkingEnabled));
    putProperty(_("2D mode"), is2Dmode, changeProperty(is2Dmode));
    putProperty(_("Old accel sensor mode"), isOldAccelSensorMode, changeProperty(isOldAccelSensorMode));
}


bool AISTSimulatorItem::store(Archive& archive)
{
    SimulatorItem::store(archive);
    return impl->store(archive);
}


bool AISTSimulatorItem::Impl::store(Archive& archive)
{
    archive.write("dynamicsMode", dynamicsMode.selectedSymbol(), DOUBLE_QUOTED);
    archive.write("integrationMode", integrationMode.is(SemiImplicitEuler) ? "semi-implicit_euler" : "runge-kutta");
    write(archive, "gravity", gravity);
    archive.write("min_friction_coefficient", minFrictionCoefficient);
    archive.write("max_friction_coefficient", maxFrictionCoefficient);
    archive.write("cullingThresh", contactCullingDistance);
    archive.write("contactCullingDepth", contactCullingDepth);
    archive.write("errorCriterion", errorCriterion);
    archive.write("maxNumIterations", maxNumIterations);
    archive.write("contactCorrectionDepth", contactCorrectionDepth);
    archive.write("contactCorrectionVelocityRatio", contactCorrectionVelocityRatio);
    archive.write("kinematicWalking", isKinematicWalkingEnabled);
    archive.write("2Dmode", is2Dmode);
    archive.write("oldAccelSensorMode", isOldAccelSensorMode);
    return true;
}


bool AISTSimulatorItem::restore(const Archive& archive)
{
    SimulatorItem::restore(archive);
    return impl->restore(archive);
}


bool AISTSimulatorItem::Impl::restore(const Archive& archive)
{
    string symbol;
    if(archive.read("dynamicsMode", symbol)){
        dynamicsMode.select(symbol);
    }
    if(archive.read("integrationMode", symbol)){
        if(symbol == "runge-kutta" || symbol == "Runge Kutta"){
            integrationMode.select(RungeKutta);
        } else {
            integrationMode.select(SemiImplicitEuler);
        }
    }
    read(archive, "gravity", gravity);
    archive.read("min_friction_coefficient", minFrictionCoefficient);
    archive.read("max_friction_coefficient", maxFrictionCoefficient);
    contactCullingDistance = archive.get("cullingThresh", contactCullingDistance.string());
    contactCullingDepth = archive.get("contactCullingDepth", contactCullingDepth.string());
    errorCriterion = archive.get("errorCriterion", errorCriterion.string());
    archive.read("maxNumIterations", maxNumIterations);
    contactCorrectionDepth = archive.get("contactCorrectionDepth", contactCorrectionDepth.string());
    contactCorrectionVelocityRatio = archive.get("contactCorrectionVelocityRatio", contactCorrectionVelocityRatio.string());
    archive.read("kinematicWalking", isKinematicWalkingEnabled);
    archive.read("2Dmode", is2Dmode);
    archive.read("oldAccelSensorMode", isOldAccelSensorMode);
    return true;
}
