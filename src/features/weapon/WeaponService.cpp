#include "WeaponService.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include <array>

namespace TutonesV2::Features::Weapon
{
    namespace {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
        constexpr std::array<const char*, 62> Weapons{{
            "WEAPON_KNIFE","WEAPON_NIGHTSTICK","WEAPON_HAMMER","WEAPON_BAT","WEAPON_CROWBAR",
            "WEAPON_PISTOL","WEAPON_COMBATPISTOL","WEAPON_APPISTOL","WEAPON_PISTOL50","WEAPON_HEAVYPISTOL",
            "WEAPON_SNSPISTOL","WEAPON_PISTOL_MK2","WEAPON_SNSPISTOL_MK2","WEAPON_REVOLVER","WEAPON_REVOLVER_MK2",
            "WEAPON_MICROSMG","WEAPON_SMG","WEAPON_ASSAULTSMG","WEAPON_COMBATPDW","WEAPON_MACHINEPISTOL",
            "WEAPON_MINISMG","WEAPON_SMG_MK2","WEAPON_ASSAULTRIFLE","WEAPON_CARBINERIFLE","WEAPON_ADVANCEDRIFLE",
            "WEAPON_SPECIALCARBINE","WEAPON_BULLPUPRIFLE","WEAPON_COMPACTRIFLE","WEAPON_CARBINERIFLE_MK2","WEAPON_ASSAULTRIFLE_MK2",
            "WEAPON_SPECIALCARBINE_MK2","WEAPON_BULLPUPRIFLE_MK2","WEAPON_MG","WEAPON_COMBATMG","WEAPON_COMBATMG_MK2",
            "WEAPON_PUMPSHOTGUN","WEAPON_SAWNOFFSHOTGUN","WEAPON_ASSAULTSHOTGUN","WEAPON_BULLPUPSHOTGUN","WEAPON_HEAVYSHOTGUN",
            "WEAPON_AUTOSHOTGUN","WEAPON_COMBATSHOTGUN","WEAPON_PUMPSHOTGUN_MK2","WEAPON_SNIPERRIFLE","WEAPON_HEAVYSNIPER",
            "WEAPON_MARKSMANRIFLE","WEAPON_HEAVYSNIPER_MK2","WEAPON_MARKSMANRIFLE_MK2","WEAPON_GRENADELAUNCHER","WEAPON_RPG",
            "WEAPON_MINIGUN","WEAPON_HOMINGLAUNCHER","WEAPON_COMPACTLAUNCHER","WEAPON_RAILGUN","WEAPON_RAILGUNXM3",
            "WEAPON_FIREWORK","WEAPON_GRENADE","WEAPON_STICKYBOMB","WEAPON_PROXMINE","WEAPON_PIPEBOMB",
            "WEAPON_MOLOTOV","WEAPON_EMPLAUNCHER"
        }};
        int Ped() noexcept {
            auto v=NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
            return v?*v:0;
        }
    }

    WeaponService& WeaponService::Get() noexcept { static WeaponService s; return s; }
    bool WeaponService::Initialize() noexcept { m_Ready=true; return true; }
    bool WeaponService::IsReady() const noexcept { return m_Ready.load(); }
    bool WeaponService::InfiniteAmmo() const noexcept { return m_InfiniteAmmo.load(); }
    bool WeaponService::InfiniteClip() const noexcept { return m_InfiniteClip.load(); }
    bool WeaponService::ExplosiveAmmo() const noexcept { return m_ExplosiveAmmo.load(); }

    std::uint32_t WeaponService::Joaat(const char* text) noexcept {
        std::uint32_t h{}; while(text&&*text){unsigned char c=static_cast<unsigned char>(*text++); if(c>='A'&&c<='Z')c=static_cast<unsigned char>(c-'A'+'a'); h+=c;h+=h<<10;h^=h>>6;} h+=h<<3;h^=h>>11;h+=h<<15;return h;
    }

    bool WeaponService::HasPersistentWork() const noexcept { return m_InfiniteAmmo||m_InfiniteClip||m_ExplosiveAmmo; }
    bool WeaponService::EnsureLoop() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||!HasPersistentWork()) return false;
        bool expected=false; if(!m_LoopQueued.compare_exchange_strong(expected,true)) return true;
        if(Game::GameRuntime::Get().Enqueue([this]{Tick();})) return true;
        m_LoopQueued=false; return false;
    }
    void WeaponService::Tick() noexcept {
        if(!IsReady()){m_LoopQueued=false;return;}
        const int ped=Ped();
        if(ped){
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmo,ped,std::int32_t{m_InfiniteAmmo?1:0},std::uint32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmoClip,ped,std::int32_t{m_InfiniteClip?1:0}));
            if(m_ExplosiveAmmo){
                const auto armed=NativeInvoker::Invoke<std::int32_t>(NativeId::IsPedArmed,ped,std::int32_t{4});
                const auto melee=NativeInvoker::Invoke<std::int32_t>(NativeId::IsPedPerformingMeleeAction,ped);
                if(armed&&*armed&&melee&&!*melee){
                    Game::Native::NativeVector3 impact{};
                    const auto hit=NativeInvoker::Invoke<std::int32_t>(NativeId::GetPedLastWeaponImpactCoord,ped,&impact);
                    if(hit&&*hit) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::AddOwnedExplosion,ped,impact.x,impact.y,impact.z,18,1.0f,std::int32_t{1},std::int32_t{0},0.1f));
                }
            }
        }
        if(!HasPersistentWork()){m_LoopQueued=false;return;}
        if(!Game::GameRuntime::Get().Enqueue([this]{Tick();})) m_LoopQueued=false;
    }

    bool WeaponService::SetInfiniteAmmo(bool e) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false; m_InfiniteAmmo=e;
        if(e)return EnsureLoop();
        return Game::GameRuntime::Get().Enqueue([]{const int p=Ped();if(p)static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmo,p,std::int32_t{0},std::uint32_t{0}));});
    }
    bool WeaponService::SetInfiniteClip(bool e) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false; m_InfiniteClip=e;
        if(e)return EnsureLoop();
        return Game::GameRuntime::Get().Enqueue([]{const int p=Ped();if(p)static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmoClip,p,std::int32_t{0}));});
    }
    bool WeaponService::SetExplosiveAmmo(bool e) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false; m_ExplosiveAmmo=e; return !e||EnsureLoop();
    }

    bool WeaponService::QueueGiveWeapon(std::string name) noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady()||name.empty())return false;
        return Game::GameRuntime::Get().Enqueue([n=std::move(name)]{const int p=Ped();if(p)static_cast<void>(NativeInvoker::InvokeVoid(NativeId::GiveWeaponToPed,p,Joaat(n.c_str()),9999,std::int32_t{0},std::int32_t{0}));});
    }
    bool WeaponService::QueueGiveAllWeapons() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        return Game::GameRuntime::Get().Enqueue([]{const int p=Ped();if(!p)return;for(auto* n:Weapons)static_cast<void>(NativeInvoker::InvokeVoid(NativeId::GiveWeaponToPed,p,Joaat(n),9999,std::int32_t{0},std::int32_t{0}));});
    }
    bool WeaponService::QueueMaxAmmo() noexcept {
        if(!IsReady()||!Game::GameRuntime::Get().NativeReady())return false;
        return Game::GameRuntime::Get().Enqueue([]{const int p=Ped();if(!p)return;for(auto* n:Weapons){const auto w=Joaat(n);const auto has=NativeInvoker::Invoke<std::int32_t>(NativeId::HasPedGotWeapon,p,w,std::int32_t{0});if(!has||!*has)continue;int max{};const auto ok=NativeInvoker::Invoke<std::int32_t>(NativeId::GetMaxAmmo,p,w,&max);if(ok&&*ok&&max>0)static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedAmmo,p,w,max,std::int32_t{0}));}});
    }

    void WeaponService::Shutdown() noexcept {
        if(!m_Ready.exchange(false))return; m_InfiniteAmmo=false;m_InfiniteClip=false;m_ExplosiveAmmo=false;m_LoopQueued=false;
        if(Game::GameRuntime::Get().NativeReady()) static_cast<void>(Game::GameRuntime::Get().Enqueue([]{const int p=Ped();if(!p)return;static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmo,p,std::int32_t{0},std::uint32_t{0}));static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedInfiniteAmmoClip,p,std::int32_t{0}));}));
    }
}
