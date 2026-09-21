#include "TeleportService.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include <cmath>
#include <utility>

namespace TutonesV2::Features::World
{
    namespace {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
        constexpr int MaxAttempts=120;

        bool Exists(int entity) noexcept {
            if(!entity)return false;
            const auto e=NativeInvoker::Invoke<std::int32_t>(NativeId::DoesEntityExist,entity);
            return e&&*e;
        }
    }

    TeleportService& TeleportService::Get() noexcept { static TeleportService s;return s; }
    bool TeleportService::Initialize() noexcept {m_Pending=false;{std::scoped_lock l(m_Mutex);m_Snapshot={};}m_Ready=true;return true;}
    void TeleportService::Shutdown() noexcept {m_Ready=false;if(m_Pending&&Game::GameRuntime::Get().NativeReady())static_cast<void>(Game::GameRuntime::Get().Enqueue([this]{Restore("Teleport cancelled during shutdown");}));}
    bool TeleportService::IsReady() const noexcept{return m_Ready.load();}
    TeleportSnapshot TeleportService::Snapshot() const{std::scoped_lock l(m_Mutex);auto s=m_Snapshot;s.pending=m_Pending.load();return s;}

    TeleportService::Target TeleportService::ResolveTarget() noexcept {
        Target t{};
        const auto ped=NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
        if(!ped||!*ped)return t;
        t.entity=*ped;
        const auto in=NativeInvoker::Invoke<std::int32_t>(NativeId::IsPedInAnyVehicle,*ped,std::int32_t{0});
        if(in&&*in){
            const auto v=NativeInvoker::Invoke<std::int32_t>(NativeId::GetVehiclePedIsIn,*ped,std::int32_t{0});
            if(v&&*v&&Exists(*v)){t.entity=*v;t.vehicle=*v;t.inVehicle=true;}
        }
        return t;
    }

    bool TeleportService::QueueWaypoint() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        bool expected=false;if(!m_Pending.compare_exchange_strong(expected,true))return false;
        {std::scoped_lock l(m_Mutex);m_Snapshot.message="Resolving waypoint...";m_Snapshot.lastSucceeded=false;}
        if(Game::GameRuntime::Get().Enqueue([this]{
            const auto active=NativeInvoker::Invoke<std::int32_t>(NativeId::IsWaypointActive);
            if(!active||!*active){Finish(false,"Set a waypoint first");return;}
            const auto en=NativeInvoker::Invoke<std::int32_t>(NativeId::GetWaypointBlipEnumId);
            if(!en||!*en){Finish(false,"Waypoint blip type unavailable");return;}
            const auto blip=NativeInvoker::Invoke<std::int32_t>(NativeId::GetClosestBlipInfoId,*en);
            if(!blip||!*blip){Finish(false,"Waypoint blip unavailable");return;}
            const auto exists=NativeInvoker::Invoke<std::int32_t>(NativeId::DoesBlipExist,*blip);
            if(!exists||!*exists){Finish(false,"Waypoint disappeared");return;}
            const auto coords=NativeInvoker::Invoke<Game::Native::NativeVector3>(NativeId::GetBlipCoords,*blip);
            if(!coords){Finish(false,"Waypoint coordinates unavailable");return;}
            BeginTeleport(*coords,true,"Waypoint teleport");
        }))return true;
        m_Pending=false;return false;
    }

    bool TeleportService::QueueCoordinates(float x,float y,float z,bool resolveGround) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
        bool expected=false;if(!m_Pending.compare_exchange_strong(expected,true))return false;
        {std::scoped_lock l(m_Mutex);m_Snapshot.message="Coordinate teleport queued";m_Snapshot.lastSucceeded=false;}
        if(Game::GameRuntime::Get().Enqueue([this,x,y,z,resolveGround]{BeginTeleport({x,y,z},resolveGround,"Coordinate teleport");}))return true;
        m_Pending=false;return false;
    }

    bool TeleportService::BeginTeleport(Game::Native::NativeVector3 coords,bool resolveGround,std::string label) noexcept {
        m_Target=ResolveTarget();
        if(!m_Target.entity){Finish(false,"Player/vehicle target unavailable");return false;}
        const auto original=NativeInvoker::Invoke<Game::Native::NativeVector3>(NativeId::GetEntityCoords,m_Target.entity,std::int32_t{0});
        if(!original){Finish(false,"Could not capture original position");return false;}
        m_Original=*original;m_Destination=coords;m_ResolveGround=resolveGround;m_Label=std::move(label);m_Attempts=0;
        if(!Freeze(true)){Finish(false,"Could not freeze teleport target");return false;}
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVelocity,m_Target.entity,0.0f,0.0f,0.0f));

        if(resolveGround){
            m_Destination.z=1000.0f;
            StreamCollision(m_Destination);
            if(!Move(m_Destination)){Restore("Initial teleport staging failed");return false;}
            {std::scoped_lock l(m_Mutex);m_Snapshot.message=m_Label+": resolving ground/collision";}
            if(!Game::GameRuntime::Get().Enqueue([this]{GroundTick();}))Restore("Ground-resolution queue unavailable");
        }else{
            StreamCollision(m_Destination);
            if(!Move(m_Destination)){Restore("Teleport placement failed");return false;}
            {std::scoped_lock l(m_Mutex);m_Snapshot.message=m_Label+": streaming collision";}
            if(!Game::GameRuntime::Get().Enqueue([this]{SettleTick();}))Restore("Collision-settle queue unavailable");
        }
        return true;
    }

    void TeleportService::GroundTick() noexcept {
        if(!m_Pending||!Exists(m_Target.entity)){Restore("Teleport target disappeared");return;}
        StreamCollision(m_Destination);
        float ground{};
        auto gotGround=NativeInvoker::Invoke<std::int32_t>(NativeId::GetGroundZFor3DCoord,m_Destination.x,m_Destination.y,1000.0f,&ground,std::int32_t{0},std::int32_t{0});
        float water{};
        auto gotWater=NativeInvoker::Invoke<std::int32_t>(NativeId::GetWaterHeight,m_Destination.x,m_Destination.y,m_Destination.z,&water);
        if(gotGround&&*gotGround&&std::isfinite(ground)){
            m_Destination.z=ground+1.0f;
            static_cast<void>(Move(m_Destination));
            m_Attempts=0;
            if(!Game::GameRuntime::Get().Enqueue([this]{SettleTick();}))Restore("Collision-settle queue unavailable");
            return;
        }
        if(gotWater&&*gotWater&&std::isfinite(water)){
            m_Destination.z=water;
            static_cast<void>(Move(m_Destination));
            m_Attempts=0;
            if(!Game::GameRuntime::Get().Enqueue([this]{SettleTick();}))Restore("Collision-settle queue unavailable");
            return;
        }
        if(++m_Attempts>=MaxAttempts){Restore("Ground/collision did not resolve; original position restored");return;}
        if(!Game::GameRuntime::Get().Enqueue([this]{GroundTick();}))Restore("Ground-resolution queue unavailable");
    }

    void TeleportService::SettleTick() noexcept {
        if(!m_Pending||!Exists(m_Target.entity)){Restore("Teleport target disappeared");return;}
        StreamCollision(m_Destination);
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVelocity,m_Target.entity,0.0f,0.0f,0.0f));
        const auto loaded=NativeInvoker::Invoke<std::int32_t>(NativeId::HasCollisionLoadedAroundEntity,m_Target.entity);
        if(loaded&&*loaded){
            static_cast<void>(Move(m_Destination));
            if(m_Target.inVehicle)static_cast<void>(NativeInvoker::Invoke<std::int32_t>(NativeId::SetVehicleOnGroundProperly,m_Target.vehicle,5.0f));
            static_cast<void>(Freeze(false));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVelocity,m_Target.entity,0.0f,0.0f,0.0f));
            Finish(true,m_Label+" complete");
            return;
        }
        if(++m_Attempts>=MaxAttempts){Restore("Destination collision never became safe; original position restored");return;}
        if(!Game::GameRuntime::Get().Enqueue([this]{SettleTick();}))Restore("Collision-settle queue unavailable");
    }

    bool TeleportService::Move(const Game::Native::NativeVector3& p) noexcept {
        return Exists(m_Target.entity)&&NativeInvoker::InvokeVoid(NativeId::SetEntityCoordsNoOffset,m_Target.entity,p.x,p.y,p.z,std::int32_t{1},std::int32_t{1},std::int32_t{1});
    }
    bool TeleportService::Freeze(bool enabled) noexcept {
        return Exists(m_Target.entity)&&NativeInvoker::InvokeVoid(NativeId::FreezeEntityPosition,m_Target.entity,std::int32_t{enabled?1:0});
    }
    void TeleportService::StreamCollision(const Game::Native::NativeVector3& p) noexcept {
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::RequestCollisionAtCoord,p.x,p.y,p.z));
    }
    void TeleportService::Restore(std::string message) noexcept {
        if(Exists(m_Target.entity)){StreamCollision(m_Original);static_cast<void>(Move(m_Original));static_cast<void>(Freeze(false));static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVelocity,m_Target.entity,0.0f,0.0f,0.0f));}
        Finish(false,std::move(message));
    }
    void TeleportService::Finish(bool success,std::string message) noexcept {
        m_Pending=false;m_Target={};m_Attempts=0;
        std::scoped_lock l(m_Mutex);m_Snapshot.lastSucceeded=success;m_Snapshot.message=std::move(message);
    }
}
