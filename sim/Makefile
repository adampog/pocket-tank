# pocket-tank PC simulator (LVGL v9 + SDL2)
#
# macOS: built as x86_64 by default because the Homebrew SDL2 here is the
# Intel build (/usr/local); the sim runs fine under Rosetta. `make ARCH=` for
# a native build against an arm64 SDL2. Elsewhere (Linux, CI) ARCH is empty
# and the host compiler's own architecture is used.

# Board tank sizes, each in its own build dir with its own binary (objects are
# never shared, so the binaries sit side by side):
#   make CYD=1    the 2.8" CYD at 320 x 240 (docs/CYD.md)   -> fishsim-cyd
#   make ROUND=1  the round 466 px bowl (Waveshare 1.75C)   -> fishsim-round
#   make WATCH=1  the 410 x 502 watch (Waveshare 2.06)      -> fishsim-watch
ifeq ($(CYD),1)
SIZE    := -DTANK_W=320 -DTANK_H=240
BUILD   := build-cyd
OUT     := fishsim-cyd
ROUNDF  :=
else ifdef ROUND
SIZE    :=
BUILD   := build-round
OUT     := fishsim-round
ROUNDF  := -DTANK_ROUND
else ifdef WATCH
SIZE    :=
BUILD   := build-watch
OUT     := fishsim-watch
ROUNDF  := -DTANK_WATCH
else
SIZE    :=
BUILD   := build
OUT     := fishsim
ROUNDF  :=
endif

CC      := cc
ifeq ($(shell uname -s),Darwin)
ARCH    ?= -arch x86_64
endif
ARCH    ?=
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

CFLAGS  := $(ROUNDF) -O2 -std=c11 $(ARCH) $(SIZE) -I. -I../common -I../common/llm -DLV_CONF_INCLUDE_SIMPLE -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --always --dirty --exclude='*' 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm -lpthread

APP_SRCS  := main.c advisor_llm.c persist_port_sim.c net_port_sim.c ../common/tank.c ../common/update.c ../common/advisor.c ../common/render.c ../common/icons.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

LVGL_SRCS := $(shell find lvgl/src -name '*.c')
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(filter-out ../common/%,$(APP_SRCS)) $(LVGL_SRCS)) $(patsubst ../common/%.c,$(BUILD)/common/%.o,$(filter ../common/%,$(APP_SRCS)))

$(OUT): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

SELFTESTS := --selftest --selftest-hunger --selftest-shop --selftest-battery \
             --selftest-pop --selftest-card --selftest-sleep --selftest-tend --selftest-llm \
             --selftest-saves --selftest-update
# the dev repo does not track model/out (the public one does): NOMODEL=1 runs
# the same checks without the LLM selftest, for a checkout with no model
ifdef NOMODEL
SELFTESTS := $(filter-out --selftest-llm,$(SELFTESTS))
endif

# `make check` tests the rectangle; `make ROUND=1 check` / `make WATCH=1 check`
# the same selftests in that board's world; `make check-all` all three - the
# gate for anything in common/ (docs/BOARDS.md)
check: $(OUT)
	@set -e; for test in $(SELFTESTS); do echo "RUN $(OUT) $$test"; ./$(OUT) "$$test"; done

check-all:
	$(MAKE) check
	$(MAKE) CYD=1 check
	$(MAKE) ROUND=1 check
	$(MAKE) WATCH=1 check

check-nomodel:
	$(MAKE) NOMODEL=1 check

$(BUILD)/common/llm/%.o: ../common/llm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

$(BUILD)/common/%.o: ../common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build fishsim build-cyd fishsim-cyd build-round build-watch fishsim-round fishsim-watch

-include $(OBJS:.o=.d)

.PHONY: check check-all check-nomodel clean
