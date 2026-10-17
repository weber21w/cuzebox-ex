# Run from the repository root: make -f tests/preferences/Makefile preferences-test
include Makefile

PREF_TEST_OBJECTS = $(filter-out $(OBD)/main.o $(OBD)/winicon.o,$(OBJECTS))

.PHONY: interrupt-test
interrupt-test: $(OBD)/interrupt_regression.exe
	mkdir -p $(OBD)/interrupt-run
	cd $(OBD)/interrupt-run && ../interrupt_regression.exe

$(OBD)/interrupt_regression.exe: tests/bootloader/interrupt_regression.c main.c cu_avr.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -I. -o $@ $< $(filter-out $(OBD)/cu_avr.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSPD)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: timer1-test
timer1-test: $(OBD)/timer1_regression.exe
	mkdir -p $(OBD)/timer1-run
	cd $(OBD)/timer1-run && ../timer1_regression.exe

$(OBD)/timer1_regression.exe: tests/bootloader/timer1_regression.c tests/preferences/Makefile cu_avr.c main.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -I. -o $@ $< $(filter-out $(OBD)/cu_avr.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSPD)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

# Local comparison only: snapshot cu_avr.c to _obj_/timer1-baseline.c before editing.
$(OBD)/timer1_old_cpu.o: _obj_/timer1-baseline.c $(DEPS) | $(OBD)
	$(CC) -I. -c $< -o $@ $(filter-out -flto -flto=auto,$(CFSPD))
$(OBD)/timer1_new_cpu.o: cu_avr.c $(DEPS) | $(OBD)
	$(CC) -I. -c $< -o $@ $(filter-out -flto -flto=auto,$(CFSPD))
$(OBD)/timer1_cpu_old.exe: tests/bootloader/timer1_cpu_benchmark.c $(OBD)/timer1_old_cpu.o $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -I. -o $@ $< $(OBD)/timer1_old_cpu.o $(filter-out $(OBD)/cu_avr.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSPD)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))
$(OBD)/timer1_cpu_new.exe: tests/bootloader/timer1_cpu_benchmark.c $(OBD)/timer1_new_cpu.o $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -I. -o $@ $< $(OBD)/timer1_new_cpu.o $(filter-out $(OBD)/cu_avr.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSPD)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: headless-test
headless-test: $(OBD)/headless_regression.exe
	./$(OBD)/headless_regression.exe

$(OBD)/headless_regression.exe: tests/gui/headless_regression.c guicore.c audio.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(filter-out $(OBD)/guicore.o $(OBD)/audio.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: screenshot-test
screenshot-test: $(OBD)/screenshot_regression.exe
	mkdir -p $(OBD)/screenshot-run
	cd $(OBD)/screenshot-run && ../screenshot_regression.exe

$(OBD)/screenshot_regression.exe: tests/gui/screenshot_regression.c main.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(PREF_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: source-step-test
source-step-test: $(OBD)/source_step_regression.exe
	mkdir -p $(OBD)/source-step-run
	cd $(OBD)/source-step-run && ../source_step_regression.exe

$(OBD)/source_step_regression.exe: tests/bootloader/source_step_regression.c main.c debug_source.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(filter-out $(OBD)/debug_source.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

WHISPER_TEST_OBJECTS = $(filter-out $(OBD)/cu_avr.o,$(PREF_TEST_OBJECTS))
.PHONY: whisper-test
whisper-test: $(OBD)/whisper_on.exe $(OBD)/whisper_off.exe
	./$(OBD)/whisper_on.exe
	./$(OBD)/whisper_off.exe

$(OBD)/whisper_on.exe: tests/bootloader/whisper_regression.c cu_avr.c $(WHISPER_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(WHISPER_TEST_OBJECTS) $(filter-out -Dmain=SDL_main -DFLAG_PRINTF_WHISPER=%,$(CFSIZ)) -DFLAG_PRINTF_WHISPER=1 $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

$(OBD)/whisper_off.exe: tests/bootloader/whisper_regression.c cu_avr.c $(WHISPER_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(WHISPER_TEST_OBJECTS) $(filter-out -Dmain=SDL_main -DFLAG_PRINTF_WHISPER=%,$(CFSIZ)) -DFLAG_PRINTF_WHISPER=0 $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: rom-loading-test
rom-loading-test: $(OBD)/rom_loading_regression.exe
	mkdir -p $(OBD)/rom-loading-run
	cd $(OBD)/rom-loading-run && ../rom_loading_regression.exe

$(OBD)/rom_loading_regression.exe: tests/bootloader/rom_loading_regression.c main.c $(PREF_TEST_OBJECTS) $(DEPS) | $(OBD)
	$(CC) -o $@ $< $(PREF_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: preferences-test
preferences-test: $(OBD)/preferences_regression.exe
	mkdir -p $(OBD)/preferences-run
	cd $(OBD)/preferences-run && ../preferences_regression.exe

$(OBD)/preferences_regression.exe: tests/preferences/preferences_regression.c main.c $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(PREF_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

# Intentionally omit ENABLE_ESP and all application feature defines.
.PHONY: lean-preferences-test
lean-preferences-test: $(OBD)/lean-preferences.exe
	./$(OBD)/lean-preferences.exe

$(OBD)/lean-preferences.exe: tests/preferences/lean_preferences.c configcfg.c shortcuts.c $(DEPS) | $(OBD)
	$(CC) -std=gnu17 -I. $(if $(CC_INC),-I$(CC_INC)) $< configcfg.c shortcuts.c -o $@

# Uses the full SDL2/MicroUI configuration; no native window is needed.
GUI_TEST_OBJECTS = $(filter-out $(OBD)/main.o $(OBD)/mui_integration.o $(OBD)/winicon.o,$(OBJECTS))
.PHONY: cheats-test
cheats-test: $(OBD)/cheats_regression.exe
	mkdir -p $(OBD)/cheats-run
	cd $(OBD)/cheats-run && ../cheats_regression.exe

$(OBD)/cheats_regression.exe: tests/gui/cheats_regression.c main.c microui/mui_integration.c $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(GUI_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: shortcuts-test
shortcuts-test: $(OBD)/shortcuts_regression.exe
	mkdir -p $(OBD)/shortcuts-run
	cd $(OBD)/shortcuts-run && ../shortcuts_regression.exe

$(OBD)/shortcuts_regression.exe: tests/gui/shortcuts_regression.c main.c ginput.c microui/mui_integration.c microui/mui_shortcuts.h $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(filter-out $(OBD)/ginput.o,$(GUI_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: controller-mapping-test
controller-mapping-test: $(OBD)/controller_mapping_regression.exe
	mkdir -p $(OBD)/controller-mapping-run
	cd $(OBD)/controller-mapping-run && ../controller_mapping_regression.exe

$(OBD)/controller_mapping_regression.exe: tests/gui/controller_mapping_regression.c main.c ginput.c microui/mui_integration.c microui/mui_controller_mapping.h $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(filter-out $(OBD)/ginput.o,$(GUI_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: screenshot-tool-test
screenshot-tool-test: $(OBD)/screenshot_tool_regression.exe
	mkdir -p $(OBD)/screenshot-tool-run
	cd $(OBD)/screenshot-tool-run && ../screenshot_tool_regression.exe
$(OBD)/screenshot_tool_regression.exe: tests/gui/screenshot_tool_regression.c main.c microui/mui_integration.c textgui.c $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(filter-out $(OBD)/textgui.o,$(GUI_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))
.PHONY: input-playback-test
input-playback-test: $(OBD)/input_playback_regression.exe
	mkdir -p $(OBD)/input-playback-run
	cd $(OBD)/input-playback-run && ../input_playback_regression.exe

$(OBD)/input_playback_regression.exe: tests/gui/input_playback_regression.c main.c microui/mui_integration.c $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(GUI_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

$(GUI_TEST_OBJECTS): | $(OBD)
.PHONY: filedialog-test
filedialog-test: $(OBD)/filedialog_regression.exe
	mkdir -p $(OBD)/filedialog-run
	cd $(OBD)/filedialog-run && ../filedialog_regression.exe

$(OBD)/filedialog_regression.exe: tests/gui/filedialog_regression.c main.c microui/mui_integration.c $(GUI_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(GUI_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: input-movie-test
input-movie-test: $(OBD)/input_movie_regression.exe
	mkdir -p $(OBD)/input-movie-run
	cd $(OBD)/input-movie-run && ../input_movie_regression.exe

$(OBD)/input_movie_regression.exe: tests/input_movie/movie_regression.c main.c $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(PREF_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

.PHONY: storage-test
storage-test: $(OBD)/storage_regression.exe
	mkdir -p $(OBD)/storage-run
	cd $(OBD)/storage-run && ../storage_regression.exe

$(OBD)/storage_regression.exe: tests/input_movie/storage_regression.c main.c capture.c savestate.c $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(filter-out $(OBD)/capture.o $(OBD)/savestate.o,$(PREF_TEST_OBJECTS)) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))

$(OBD)/movie_rom_roundtrip.exe: tests/input_movie/rom_roundtrip.c main.c $(PREF_TEST_OBJECTS) $(DEPS)
	$(CC) -o $@ $< $(PREF_TEST_OBJECTS) $(filter-out -Dmain=SDL_main,$(CFSIZ)) $(filter-out -mwindows -lSDL2main -lmingw32,$(LINKB))
