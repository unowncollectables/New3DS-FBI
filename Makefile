TARGET := 3DS

NAME := New3DS-FBI
TITLE := New3DS FBI

BUILD_DIR := build
OUTPUT_DIR := output
INCLUDE_DIRS := include
SOURCE_DIRS := source
ROMFS_DIR := romfs

LIBRARY_DIRS += $(DEVKITPRO)/libctru $(DEVKITPRO)/portlibs/armv6k $(DEVKITPRO)/portlibs/3ds
LIBRARIES += curl mbedtls mbedx509 mbedcrypto jansson z citro2d citro3d ctru

EXTRA_OUTPUT_FILES := servefiles

BUILD_FLAGS := -Wno-format-truncation -D__3DS__

# New 2DS/3DS only: extended app memory + 804MHz + L2 (RSF/CIA).
SYSTEM_MODE_EXT := 124MB
CPU_SPEED := 804MHz
ENABLE_L2_CACHE := true

# App semver (also used by self-update checker).
VERSION_MAJOR := 3
VERSION_MINOR := 5
VERSION_MICRO := 2

DESCRIPTION := Based on FBI by Steveice10. New 2DS/3DS title manager.
AUTHOR := unowncollectables

PRODUCT_CODE := CTR-P-N3FB
UNIQUE_ID := 0xF83FB

ICON_FLAGS := --flags visible,ratingrequired,recordusage --cero 153 --esrb 153 --usk 153 --pegigen 153 --pegiptr 153 --pegibbfc 153 --cob 153 --grb 153 --cgsrr 153

BANNER_AUDIO := meta/audio_3ds.wav
BANNER_IMAGE := meta/banner_3ds.png
ICON := meta/icon_3ds.png
LOGO := meta/logo_3ds.bcma.lz

# INTERNAL #

include buildtools/make_base
