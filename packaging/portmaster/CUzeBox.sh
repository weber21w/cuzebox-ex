#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

# Ensure absolute path even if $directory lacks a leading slash
ROOT_PATH="${directory:-/roms}"
[[ "$ROOT_PATH" != /* ]] && ROOT_PATH="/$ROOT_PATH"

GAMEDIR="$ROOT_PATH/ports/CUzeBox"
GAME="cuzebox"

# CD and set permissions
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1
rm -rf "$GAMEDIR/config/gamelog.txt"
$ESUDO chmod +x -R $GAMEDIR/*

# Set config dir
bind_directories ~/.$GAME $GAMEDIR/config

# Exports
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export TEXTINPUTPRESET=$CHEATS
export TEXTINPUTINTERACTIVE="Y"
export TEXTINPUTNUMBERSONLY="Y"

# Note: Your binary is packaged as plain 'cuzebox' (or '.compat' if glibc < 234)
if [ "${CFW_GLIBC:-999}" -lt 234 ] 2>/dev/null; then
    GAME="cuzebox.compat"
else
    GAME="cuzebox"
fi

# Run game
$GPTOKEYB "$GAME" -c "config/joy.gptk" &
pm_platform_helper "$GAMEDIR/$GAME"
./$GAME

# Cleanup
pm_finish
