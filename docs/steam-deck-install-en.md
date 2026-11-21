# Installing BornRok on a Steam Deck (without the Steam store)

You need the portable archive **`BornRok-steamos-x86_64.tar.gz`**. It is built with
`VCPKG_ROOT=~/vcpkg ./scripts/build-steam.sh` (no Steamworks account needed).

1. **Switch the Deck to Desktop Mode.** Press the Steam button, open **Power**, then **Switch to Desktop**.
2. **Copy the archive to the Deck** (USB stick, network share or a download), for example to `~/Downloads/`.
3. **Unpack it.**
   1. Open the **Dolphin** file manager and create the folder `~/Games/BornRok/`.
   2. Right-click the archive, choose **Extract → Extract archive here**, and move the files into that folder.
   3. You should see `BornRok`, `run.sh` and `bornrok-userdata.txt`. Don't delete `bornrok-userdata.txt`.
4. **Add it to Steam.**
   1. Open Steam on the desktop and choose **Games → Add a Non-Steam Game to My Library…**.
   2. Click **Browse…**, set the file type filter to **All Files**, select `~/Games/BornRok/run.sh` and click **Add Selected Programs**.
5. **Go back to Gaming Mode** with the **Return to Gaming Mode** icon on the desktop. BornRok is in your Library under **Non-Steam**.
6. **First launch.** The built-in patcher downloads the game data into `~/.local/share/uaro/client/`. This takes a while once; later launches only fetch updates.

**Controls.** The gamepad works through Steam Input like any non-Steam game. If the buttons feel wrong,
open the game's **Controller settings** in Steam and pick the **Gamepad** layout.

**Updating the program.** Unpack a newer archive over `~/Games/BornRok/`. Your game data, settings and
logs stay in `~/.local/share/uaro/client/` and are kept.

**Removing it.** Remove the shortcut from Steam, then delete `~/Games/BornRok/` and `~/.local/share/uaro/client/`.
