/*
    Weapons on Any Rig: characters on any rig can hold weapons, as long as their costume has a hand attachment.

    Before it shows a weapon, the game checks two things: that the costume's rig is called "humanoid", and then that
    the costume has an attachment point called "hand". It does this in two places: for a character in the world
    (mmoCharacter::EquipWeaponModel), and in the costume editor's weapon list (mmoCostumeEditorView::_UpdateGrid).

    In both, the rig check is a call to the string compare with the text "humanoid". This plugin makes that compare
    say yes, so the hand check is the only rule left: rigs without a hand still get no weapon, and humanoids don't
    change at all. If a game update changes either place so it can't be found, the plugin changes nothing, and the
    log says why.
*/
#include <mt2loader.hpp>


void plugin::init() {
    game::in("mmoCharacter::EquipWeaponModel").call("std::operator==").with_text("humanoid").returns(true);
    game::in("mmoCostumeEditorView::_UpdateGrid").call("std::operator==").with_text("humanoid").returns(true);

    plugin::log("Any rig with a hand attachment can hold weapons");
}
