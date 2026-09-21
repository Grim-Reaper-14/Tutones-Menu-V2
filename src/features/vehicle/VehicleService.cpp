#include "VehicleService.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
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
        m_Busy=false;m_LoopQueued=false;m_PendingModel=0;m_Attempts=0;
        {std::scoped_lock lock(m_Mutex);m_Snapshot={};}
        m_Ready=true; return true;
    }
    void VehicleService::Shutdown() noexcept {
        m_Ready=false;m_Busy=false;m_LoopQueued=false;m_PendingModel=0;
    }
    bool VehicleService::IsReady() const noexcept { return m_Ready.load(); }
    VehicleSnapshot VehicleService::Snapshot() const { std::scoped_lock lock(m_Mutex); auto s=m_Snapshot; s.busy=m_Busy.load(); return s; }

    std::uint32_t VehicleService::Joaat(const std::string& value) noexcept {
        std::uint32_t h{};for(unsigned char c:value){if(c>='A'&&c<='Z')c=static_cast<unsigned char>(c-'A'+'a');h+=c;h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;h+=h<<15;return h;
    }

    bool VehicleService::QueueSpawn(std::string name,bool enterVehicle,bool networked) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||name.empty())return false;
        bool expected=false;if(!m_Busy.compare_exchange_strong(expected,true))return false;
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.message="Validating vehicle model...";
            m_Snapshot.lastSucceeded=false;
        }
        const auto model=Joaat(name);
        if(Game::GameRuntime::Get().Enqueue([this,model,enterVehicle,networked]{
            const auto cd=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelInCdimage,model);
            const auto valid=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelValid,model);
            const auto vehicle=NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelAVehicle,model);
            if(!cd||!*cd||!valid||!*valid||!vehicle||!*vehicle){Finish(false,0,"Invalid vehicle model");return;}
            if(!NativeInvoker::InvokeVoid(NativeId::RequestModel,model)){Finish(false,0,"REQUEST_MODEL failed");return;}
            m_EnterVehicle=enterVehicle;m_Networked=networked;m_Attempts=0;m_PendingModel=model;
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
        if(m_EnterVehicle) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedIntoVehicle,ped,veh,-1));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded,model));
        m_PendingModel=0;m_LoopQueued=false;
        Finish(persisted,veh,persisted?"Vehicle spawned":"Vehicle spawned, but network persistence setup failed");
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
}
