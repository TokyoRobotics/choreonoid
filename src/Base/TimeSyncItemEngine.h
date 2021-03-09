/**
   \file
   \author Shin'ichiro Nakaoka
*/

#ifndef CNOID_BASE_TIME_SYNC_ITEM_ENGINE_H
#define CNOID_BASE_TIME_SYNC_ITEM_ENGINE_H

#include "Item.h"
#include <functional>
#include <typeinfo>
#include <vector>
#include "exportdecl.h"

namespace cnoid {

class CNOID_EXPORT TimeSyncItemEngine : public Referenced
{
public:
    TimeSyncItemEngine(Item* item);
    virtual ~TimeSyncItemEngine();

    TimeSyncItemEngine(const TimeSyncItemEngine& org) = delete;
    TimeSyncItemEngine& operator=(const TimeSyncItemEngine& rhs) = delete;

    Item* item(){ return item_; }

    virtual bool onPlaybackInitialized(double time);
    virtual void onPlaybackStarted(double time);
    virtual bool onTimeChanged(double time) = 0;
    virtual void onPlaybackStopped(double time, bool isStoppedManually);
    virtual bool isTimeSyncAlwaysMaintained() const;
    
    bool startUpdatingTime();
    void updateTime(double time);
    void stopUpdatingTime();
    
    void refresh();

    // Accessed by the manager
    bool isTimeSyncForcedToBeMaintained() const { return isTimeSyncForcedToBeMaintained_; }

private:
    ItemPtr item_;
    int fillLevelId;
    bool isTimeSyncForcedToBeMaintained_;

    friend class TimeSyncItemEngineManager;
};

typedef ref_ptr<TimeSyncItemEngine> TimeSyncItemEnginePtr;

class ExtensionManager;
class Item;

class CNOID_EXPORT TimeSyncItemEngineManager
{
public:
    static void initializeClass(ExtensionManager* ext);
    static TimeSyncItemEngineManager* instance();

    ~TimeSyncItemEngineManager();

    template<class ItemType, class EngineType = TimeSyncItemEngine>
    void registerFactory(std::function<TimeSyncItemEngine*(ItemType* item, EngineType* prevEngine)> factory){
        registerFactory_(
            typeid(ItemType),
            [factory](Item* item, TimeSyncItemEngine* prevEngine){
                return factory(static_cast<ItemType*>(item), dynamic_cast<EngineType*>(prevEngine));
            });
    }

    /**
       This function create engines for the item and add engines to io_engines.
       \return The number of created engines
    */
    int createEngines(Item* item, std::vector<TimeSyncItemEnginePtr>& io_engines);

    class Impl;

private:
    TimeSyncItemEngineManager();

    void registerFactory_(
        const std::type_info& type,
        const std::function<TimeSyncItemEngine*(Item* item, TimeSyncItemEngine* prevEngine)>& factory);

    Impl* impl;
};

}

#endif
