#include <metahook.h>
#include "data/WeaponResource.h"
#include "data/NetworkMessages.h"
#include <abc/Render.h>
#include <abc/NetMsg.h>
#include <cstdlib>
#include <iostream>
#include <weaponinfo.h>
#define CHECK(x) do { if(!(x)) { std::cerr << __LINE__ << ": " << #x << "\n"; std::abort(); } } while(0)
cl_enginefunc_t gEngfuncs{};
namespace netmsg {
abc::CallbackList<void(int,int)> AmmoX;
abc::CallbackList<void(const WeaponListData&)> WeaponList;
abc::CallbackList<void(int,const char*)> CustWeapon;
abc::CallbackList<void(int,int,int,int)> CurWeapon;
abc::CallbackList<void(int,const char*)> WeaponSpr;
}
namespace abc {
IMetaRenderer* MetaRenderer() { return nullptr; }
cvar_t* CVAR_GET_POINTER(const char*) { return nullptr; }
void EngineClientCmd(const char*) {}
void ConsoleWarn(const char*,...) {}
}
namespace hudstate { bool hasSuit() { return true; } bool isSpectate() { return false; } }
static client_sprite_t* List(char*,int* count) { *count=0; return nullptr; }
int main() {
    gEngfuncs.pfnSPR_GetList=List;
    weaponres::init();
    CHECK(weaponres::kMaxWeaponIds==64);
    const unsigned char wire[]={63,0}; abc::MsgReader reader(wire,2); CHECK(reader.readShort()==63 && reader.ok());
    for(int id : {31,32,63,64}) { netmsg::WeaponListData data; data.id=id; data.name="weapon_"+std::to_string(id); data.slot=1; data.slotPos=id; data.ammoType=127; netmsg::WeaponList(data); }
    CHECK(weaponres::getWeapon(32)); CHECK(weaponres::getWeapon(63)); CHECK(!weaponres::getWeapon(64));
    auto* stable=weaponres::getWeapon(32);
    netmsg::WeaponListData updated; updated.id=32; updated.name="weapon_new"; updated.slot=2; updated.slotPos=0; updated.ammoType=127;
    netmsg::WeaponList(updated);
    CHECK(weaponres::getWeapon(32)==stable); CHECK(!weaponres::getWeapon("weapon_32")); CHECK(weaponres::getWeapon("weapon_new")==stable);
    CHECK(!weaponres::getWeapon(1,32)); CHECK(weaponres::getWeapon(2,0)==stable);
    netmsg::AmmoX(127,300); CHECK(weaponres::countAmmo(127)==300);
    weapon_data_t prediction[64]{}; prediction[32].m_iId=32; prediction[63].m_iId=63;
    weaponres::onTxferPredictionData(prediction); CHECK(weaponres::isOwned(stable)); CHECK(weaponres::isOwned(weaponres::getWeapon(63)));
    weaponres::selectSlot(2,1,false); CHECK(weaponres::selectedWeapon()==stable);
    bool dropped=false; auto token=weaponres::DropAllWeapons.append([&] { dropped=true; });
    netmsg::CurWeapon(1,-1,0,0); CHECK(dropped); CHECK(!weaponres::selectedWeapon()); CHECK(!weaponres::isOwned(stable));
    dropped=false;
    weaponres::onTxferPredictionData(prediction); weaponres::selectSlot(2,1,false);
    prediction[32].m_iId=0; prediction[63].m_iId=0; weaponres::onTxferPredictionData(prediction);
    CHECK(dropped && !weaponres::selectedWeapon());
    weaponres::DropAllWeapons.remove(token);
}
