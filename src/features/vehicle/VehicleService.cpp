#include "VehicleService.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include "../../game/vehicle/VehicleModels.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace TutonesV2::Features::Vehicle
{
    namespace {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
        constexpr int MaxModelAttempts=300;

        int PlayerPed() noexcept {
            const auto v=NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
            return v?*v:0;
        }

        int CurrentVehicle() noexcept {
            const int ped=PlayerPed();
            if(!ped) return 0;
            const auto in=NativeInvoker::Invoke<std::int32_t>(NativeId::IsPedInAnyVehicle,ped,std::int32_t{0});
            if(!in||!*in) return 0;
            const auto veh=NativeInvoker::Invoke<std::int32_t>(NativeId::GetVehiclePedIsIn,ped,std::int32_t{0});
            return veh?*veh:0;
        }

        bool ConfigurePersistence(int vehicle) noexcept {
            if(!vehicle) return false;
            bool ok=NativeInvoker::InvokeVoid(NativeId::SetEntityAsMissionEntity,vehicle,std::int32_t{1},std::int32_t{1});
            ok=NativeInvoker::InvokeVoid(NativeId::DecorSetInt,vehicle,"MPBitset",0)&&ok;
            const auto net=NativeInvoker::Invoke<std::int32_t>(NativeId::VehToNet,vehicle);
            if(!net||*net<=0) return false;
            ok=NativeInvoker::InvokeVoid(NativeId::SetNetworkIdExistsOnAllMachines,*net,std::int32_t{1})&&ok;
            ok=NativeInvoker::InvokeVoid(NativeId::SetNetworkIdCanMigrate,*net,std::int32_t{1})&&ok;
            return ok;
        }
    }

    VehicleService& VehicleService::Get() noexcept { static VehicleService s; return s; }

    bool VehicleService::Initialize() noexcept {
        m_Busy=false;m_LoopQueued=false;m_FeatureLoopQueued=false;m_CatalogLoopQueued=false;
        m_VehicleGodMode=false;m_KeepVehicleClean=false;m_HornBoost=false;
        m_PendingModel=0;m_Attempts=0;m_LastGodVehicle=0;m_Maxed=false;
        m_BoostSpeed=10.0f;m_WasHornPressed=false;m_CatalogCursor=0;
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot={};
            m_CatalogClasses.assign(Game::VehicleCatalogs::VehicleModels.size(), -2);
            m_CatalogDisplayNames.assign(Game::VehicleCatalogs::VehicleModels.size(), {});
        }
        m_Ready=true; return true;
    }
    void VehicleService::Shutdown() noexcept {
        if(!m_Ready.exchange(false))return;
        m_Busy=false;m_LoopQueued=false;m_FeatureLoopQueued=false;m_CatalogLoopQueued=false;m_PendingModel=0;
        m_VehicleGodMode=false;m_KeepVehicleClean=false;m_HornBoost=false;
        if(Game::GameRuntime::Get().NativeReady()){
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this]{
                RestoreGodVehicle();
                ResetHornBoost();
            }));
        }
    }
    bool VehicleService::IsReady() const noexcept { return m_Ready.load(); }
    bool VehicleService::VehicleGodMode() const noexcept { return m_VehicleGodMode.load(); }
    bool VehicleService::KeepVehicleClean() const noexcept { return m_KeepVehicleClean.load(); }
    bool VehicleService::HornBoost() const noexcept { return m_HornBoost.load(); }
    VehicleSnapshot VehicleService::Snapshot() const { std::scoped_lock lock(m_Mutex); auto s=m_Snapshot; s.busy=m_Busy.load(); return s; }

    VehicleCatalogSnapshot VehicleService::CatalogSnapshot() const {
        std::scoped_lock lock(m_Mutex);
        VehicleCatalogSnapshot snapshot{};
        snapshot.classes=m_CatalogClasses;
        snapshot.displayNames=m_CatalogDisplayNames;
        snapshot.total=m_CatalogClasses.size();
        snapshot.ready=std::min(m_CatalogCursor,snapshot.total);
        snapshot.loading=m_CatalogLoopQueued.load(std::memory_order_acquire);
        return snapshot;
    }

    void VehicleService::EnsureCatalog() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return;
        {
            std::scoped_lock lock(m_Mutex);
            if(m_CatalogCursor>=m_CatalogClasses.size())return;
        }
        bool expected=false;
        if(!m_CatalogLoopQueued.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return;
        if(!Game::GameRuntime::Get().Enqueue([this]{CatalogTick();}))
            m_CatalogLoopQueued.store(false,std::memory_order_release);
    }

    std::uint32_t VehicleService::Joaat(const std::string& value) noexcept {
        std::uint32_t h{};for(unsigned char c:value){if(c>='A'&&c<='Z')c=static_cast<unsigned char>(c-'A'+'a');h+=c;h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;h+=h<<15;return h;
    }

    bool VehicleService::QueueSpawn(std::string name,bool enterVehicle,bool networked,bool maxed) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||name.empty())return false;
        bool expected=false;if(!m_Busy.compare_exchange_strong(expected,true))return false;
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.message="Validating vehicle model...";
            m_Snapshot.lastSucceeded=false;
        }
        const auto model=Joaat(name);
        if(Game::GameRuntime::Get().Enqueue([this,model,enterVehicle,networked,maxed]{
            const auto cd=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelInCdimage,model);
            const auto valid=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelValid,model);
            const auto vehicle=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelAVehicle,model);
            if(!cd||!*cd||!valid||!*valid||!vehicle||!*vehicle){Finish(false,0,"Invalid vehicle model");return;}
            if(!NativeInvoker::InvokeVoid(NativeId::RequestModel,model)){Finish(false,0,"REQUEST_MODEL failed");return;}
            m_EnterVehicle=enterVehicle;m_Networked=networked;m_Maxed=maxed;m_Attempts=0;m_PendingModel=model;
            {
                std::scoped_lock lock(m_Mutex);
                m_Snapshot.message="Streaming vehicle model...";
            }
            if(!EnsureLoop()) Finish(false,0,"Could not schedule vehicle model loader");
        })) return true;
        m_Busy=false; return false;
    }

    bool VehicleService::EnsureLoop() noexcept {
        bool expected=false;if(!m_LoopQueued.compare_exchange_strong(expected,true))return true;
        if(Game::GameRuntime::Get().Enqueue([this]{SpawnTick();}))return true;
        m_LoopQueued=false;return false;
    }

    void VehicleService::CatalogTick() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()){
            m_CatalogLoopQueued.store(false,std::memory_order_release);
            return;
        }

        constexpr std::size_t BatchSize=12;
        std::size_t start{};
        std::size_t end{};
        {
            std::scoped_lock lock(m_Mutex);
            start=m_CatalogCursor;
            end=std::min(start+BatchSize,m_CatalogClasses.size());
        }

        for(std::size_t i=start;i<end;++i){
            const char* modelName=Game::VehicleCatalogs::VehicleModels[i];
            const auto hash=Joaat(modelName);

            int classIndex=-1;
            if(const auto vehicleClass=NativeInvoker::Invoke<std::int32_t>(NativeId::GetVehicleClassFromName,hash))
                classIndex=*vehicleClass;

            std::string display=modelName;
            if(const auto label=NativeInvoker::Invoke<const char*>(NativeId::GetDisplayNameFromVehicleModel,hash);
                label&&*label&&**label){
                if(const auto localized=NativeInvoker::Invoke<const char*>(NativeId::GetLabelText,*label);
                    localized&&*localized&&**localized&&std::string_view(*localized)!="NULL"){
                    display=*localized;
                }else{
                    display=*label;
                }
            }

            std::scoped_lock lock(m_Mutex);
            m_CatalogClasses[i]=classIndex;
            m_CatalogDisplayNames[i]=std::move(display);
            m_CatalogCursor=i+1;
        }

        bool done{};
        {
            std::scoped_lock lock(m_Mutex);
            done=m_CatalogCursor>=m_CatalogClasses.size();
        }
        if(done){
            m_CatalogLoopQueued.store(false,std::memory_order_release);
            return;
        }

        if(!Game::GameRuntime::Get().Enqueue([this]{CatalogTick();}))
            m_CatalogLoopQueued.store(false,std::memory_order_release);
    }

    bool VehicleService::MaxVehicle(int vehicle) noexcept {
        if(!vehicle)return false;
        bool ok=NativeInvoker::InvokeVoid(NativeId::SetVehicleModKit,vehicle,0);
        for(int type=0;type<50;++type){
            const auto count=NativeInvoker::Invoke<std::int32_t>(NativeId::GetNumVehicleMods,vehicle,type);
            if(count&&*count>0)
                ok=NativeInvoker::InvokeVoid(NativeId::SetVehicleMod,vehicle,type,*count-1,std::int32_t{0})&&ok;
        }
        constexpr int ToggleSlots[]{17,18,20,22};
        for(const int type:ToggleSlots)
            ok=NativeInvoker::InvokeVoid(NativeId::ToggleVehicleMod,vehicle,type,std::int32_t{1})&&ok;
        return ok;
    }

    void VehicleService::SpawnTick() noexcept {
        if(!IsReady()||!m_Busy){m_LoopQueued=false;return;}
        const auto model=m_PendingModel.load();
        if(!model){m_LoopQueued=false;return;}
        const auto loaded=NativeInvoker::Invoke<std::int32_t>(NativeId::HasModelLoaded,model);
        if(!loaded||!*loaded){
            if(++m_Attempts>=MaxModelAttempts){
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded,model));
                m_LoopQueued=false;Finish(false,0,"Vehicle model streaming timed out");return;
            }
            if(!Game::GameRuntime::Get().Enqueue([this]{SpawnTick();})){m_LoopQueued=false;Finish(false,0,"Vehicle model loader queue failed");}
            return;
        }

        const int ped=PlayerPed();
        if(!ped){m_LoopQueued=false;Finish(false,0,"Local player ped unavailable");return;}

        auto coords=NativeInvoker::Invoke<Game::Native::NativeVector3>(NativeId::GetEntityCoords,ped,std::int32_t{0});
        auto heading=NativeInvoker::Invoke<float>(NativeId::GetEntityHeading,ped);
        if(!coords||!heading){m_LoopQueued=false;Finish(false,0,"Could not resolve spawn position");return;}

        float distance=5.0f;
        Game::Native::NativeVector3 min{},max{};
        if(NativeInvoker::InvokeVoid(NativeId::GetModelDimensions,model,&min,&max)){
            const float length=max.y-min.y;
            if(std::isfinite(length)&&length>0.5f) distance=std::max(5.0f,length+2.0f);
        }
        auto ahead=NativeInvoker::Invoke<Game::Native::NativeVector3>(NativeId::GetOffsetFromEntityInWorldCoords,ped,0.0f,distance,0.0f);
        if(ahead) *coords=*ahead;

        const auto created=NativeInvoker::Invoke<std::int32_t>(
            NativeId::CreateVehicle,model,coords->x,coords->y,coords->z,*heading,
            std::int32_t{m_Networked?1:0},std::int32_t{1},std::int32_t{0});
        if(!created||!*created){
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded,model));
            m_LoopQueued=false;Finish(false,0,"CREATE_VEHICLE failed");return;
        }
        const int veh=*created;
        bool persisted=true;
        if(m_Networked) persisted=ConfigurePersistence(veh);
        else static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityAsMissionEntity,veh,std::int32_t{1},std::int32_t{1}));

        static_cast<void>(NativeInvoker::Invoke<std::int32_t>(NativeId::SetVehicleOnGroundProperly,veh,5.0f));
        if(m_Maxed) static_cast<void>(MaxVehicle(veh));
        if(m_EnterVehicle) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedIntoVehicle,ped,veh,-1));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded,model));
        m_PendingModel=0;m_LoopQueued=false;
        Finish(persisted,veh,persisted?"Vehicle spawned":"Vehicle spawned, but network persistence setup failed");
    }

    bool VehicleService::HasFeatureLoopWork() const noexcept {
        return m_VehicleGodMode.load(std::memory_order_acquire)
            || m_KeepVehicleClean.load(std::memory_order_acquire)
            || m_HornBoost.load(std::memory_order_acquire);
    }

    void VehicleService::EnsureFeatureLoop() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||!HasFeatureLoopWork())return;
        bool expected=false;
        if(!m_FeatureLoopQueued.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return;
        if(!Game::GameRuntime::Get().Enqueue([this]{FeatureTick();}))
            m_FeatureLoopQueued.store(false,std::memory_order_release);
    }

    void VehicleService::RestoreGodVehicle() noexcept {
        if(!m_LastGodVehicle)return;
        const auto exists=NativeInvoker::Invoke<std::int32_t>(NativeId::DoesEntityExist,m_LastGodVehicle);
        if(exists&&*exists)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityInvincible,m_LastGodVehicle,std::int32_t{0},std::int32_t{0}));
        m_LastGodVehicle=0;
    }

    void VehicleService::ResetHornBoost() noexcept {
        m_BoostSpeed=10.0f;
        m_WasHornPressed=false;
    }

    void VehicleService::FeatureTick() noexcept {
        if(!IsReady()||!HasFeatureLoopWork()){
            RestoreGodVehicle();
            ResetHornBoost();
            m_FeatureLoopQueued.store(false,std::memory_order_release);
            return;
        }

        const int vehicle=CurrentVehicle();

        if(m_VehicleGodMode.load(std::memory_order_acquire)){
            if(vehicle&&vehicle!=m_LastGodVehicle){
                RestoreGodVehicle();
                m_LastGodVehicle=vehicle;
            }
            if(vehicle)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityInvincible,vehicle,std::int32_t{1},std::int32_t{0}));
        }else{
            RestoreGodVehicle();
        }

        if(m_KeepVehicleClean.load(std::memory_order_acquire)&&vehicle)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleDirtLevel,vehicle,0.0f));

        if(m_HornBoost.load(std::memory_order_acquire)&&vehicle){
            const auto pressed=NativeInvoker::Invoke<std::int32_t>(NativeId::IsControlPressed,0,86);
            if(pressed&&*pressed){
                if(!m_WasHornPressed){
                    const auto speed=NativeInvoker::Invoke<float>(NativeId::GetEntitySpeed,vehicle);
                    m_BoostSpeed=std::max(10.0f,speed.value_or(10.0f));
                }
                m_BoostSpeed=std::min(200.0f,m_BoostSpeed+0.3f);
                const auto position=NativeInvoker::Invoke<Game::Native::NativeVector3>(NativeId::GetEntityCoords,vehicle,std::int32_t{0});
                const auto target=NativeInvoker::Invoke<Game::Native::NativeVector3>(
                    NativeId::GetOffsetFromEntityInWorldCoords,vehicle,0.0f,m_BoostSpeed,0.0f);
                if(position&&target){
                    static_cast<void>(NativeInvoker::InvokeVoid(
                        NativeId::SetEntityVelocity,
                        vehicle,
                        target->x-position->x,
                        target->y-position->y,
                        target->z-position->z));
                }
                m_WasHornPressed=true;
            }else{
                ResetHornBoost();
            }
        }else{
            ResetHornBoost();
        }

        if(!Game::GameRuntime::Get().Enqueue([this]{FeatureTick();}))
            m_FeatureLoopQueued.store(false,std::memory_order_release);
    }

    bool VehicleService::SetVehicleGodMode(bool enabled) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        m_VehicleGodMode.store(enabled,std::memory_order_release);
        if(enabled){EnsureFeatureLoop();return true;}
        return Game::GameRuntime::Get().Enqueue([this]{RestoreGodVehicle();});
    }

    bool VehicleService::SetKeepVehicleClean(bool enabled) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        m_KeepVehicleClean.store(enabled,std::memory_order_release);
        if(enabled)EnsureFeatureLoop();
        return true;
    }

    bool VehicleService::SetHornBoost(bool enabled) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        m_HornBoost.store(enabled,std::memory_order_release);
        if(enabled)EnsureFeatureLoop();else ResetHornBoost();
        return true;
    }

    void VehicleService::Finish(bool success,int vehicle,std::string message) noexcept {
        m_Busy=false;m_PendingModel=0;
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.lastSucceeded=success;
        if(vehicle)m_Snapshot.lastSpawnedVehicle=vehicle;
        m_Snapshot.message=std::move(message);
    }

    bool VehicleService::QueueRepairCurrent() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        return Game::GameRuntime::Get().Enqueue([this]{const int v=CurrentVehicle();if(!v){Finish(false,0,"Enter a vehicle first");return;}const bool ok=NativeInvoker::InvokeVoid(NativeId::SetVehicleFixed,v);Finish(ok,v,ok?"Vehicle repaired":"Vehicle repair failed");});
    }
    bool VehicleService::QueueCleanCurrent() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        return Game::GameRuntime::Get().Enqueue([this]{const int v=CurrentVehicle();if(!v){Finish(false,0,"Enter a vehicle first");return;}const bool ok=NativeInvoker::InvokeVoid(NativeId::SetVehicleDirtLevel,v,0.0f);Finish(ok,v,ok?"Vehicle cleaned":"Vehicle clean failed");});
    }

    bool VehicleService::QueueSetUpright() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        return Game::GameRuntime::Get().Enqueue([this]{
            const int v=CurrentVehicle();
            if(!v){Finish(false,0,"Enter a vehicle first");return;}
            const auto ok=NativeInvoker::Invoke<std::int32_t>(NativeId::SetVehicleOnGroundProperly,v,5.0f);
            const bool success=ok&&*ok!=0;
            Finish(success,v,success?"Vehicle set upright":"Set upright failed");
        });
    }
}
