MT2 Loader SDK {version}
Everything needed to write native plugins for MMORPG Tycoon 2, in C++. The full guide is LOADER_MODDING.md.
A plugin says what to change, and the loader does the rest:
  game::in("mmoCharacter::EquipWeaponModel").call("std::operator==").with_text("humanoid").returns(true);

START A PLUGIN
1. Install Visual Studio Community (free) with "Desktop development with C++".
   (Or MSYS2's MinGW-w64 with GCC 13 or newer, or clang 17 or newer: plugins are C++20.)
2. In this folder: mt2sdk new MyPlugin "My Plugin"
3. In Visual Studio: File > Open > Folder, pick MyPlugin, then Build > Build All.
   The DLL goes into MyPlugin\mod\native, and mod\ is copied into the game's mod folder.
4. Start the game. mt2loader\loader.log in the game folder shows your plugin's lines.

FIND WHAT TO CHANGE
  mt2sdk find <words>     search the game's functions and globals by name
                          (add --cpp for a line to paste into your plugin)
  mt2sdk symbols          write all of them to game_symbols.txt
  mt2sdk check <mod>      check a plugin mod the way the loader will
  mt2sdk build            which game build you have, and whether this SDK knows it

WHAT'S HERE
  mt2sdk.exe              the tool above
  include\mt2loader.hpp   the plugin API: the one file a plugin includes
  include\mt2loader.h     the C layer underneath it (only for plugins written in plain C)
  template\               what "mt2sdk new" copies
  examples\               plugins to start from: hello_plugin (the smallest), weapons_any_rig (weapons on
                          every rig with a hand: finds and patches game code safely), top_spenders (a new
                          Leaderboards tab and a Features checkbox: data files and a plugin together),
                          quest_markers (each quest giver's marker picked in a new tab, kept in the save)

The game needs the MT2 Loader installed to run plugins (MT2 Mod Manager has a button for it).
MIT license (LICENSE.txt).
