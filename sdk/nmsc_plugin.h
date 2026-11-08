/* The plugin SDK's names up to 0.6 (deprecated): nmsc_plugin.h, NmscHost and the other Nmsc* types, the NMSC_*
 * macros and the plugin's `nmsc` global. A new plugin includes hailstone_plugin.h; this header includes it and adds
 * the old names back, so a plugin written against them builds unchanged. Nothing changes for a built plugin: the
 * client loads it under either set of export names (hailstone_plugin.h, "what a plugin exports").
 *
 * The old global is a macro for `host` (`nmsc->write_chat(nmsc->host, ...)` is `host->write_chat(host->host, ...)`).
 * Define NMSC_NO_OLD_GLOBAL before the include to leave the name `nmsc` free. */
#ifndef NMSC_PLUGIN_H
#define NMSC_PLUGIN_H

#if defined(NMSC_HOST) && !defined(HAILSTONE_HOST)
#define HAILSTONE_HOST
#endif
#include "hailstone_plugin.h"

typedef PluginHostApi NmscHost;
typedef HailstoneArgs NmscArgs;
typedef HailstoneBuff NmscBuff;
typedef HailstoneCommandFn NmscCommandFn;
typedef HailstoneHooks NmscHooks;
typedef HailstoneItem NmscItem;
typedef HailstoneItemSlot NmscItemSlot;
typedef HailstoneLineFn NmscLineFn;
typedef HailstoneMapSpawn NmscMapSpawn;
typedef HailstoneOptionGetFn NmscOptionGetFn;
typedef HailstoneOptionSetFn NmscOptionSetFn;
typedef HailstoneOut NmscOut;
typedef HailstonePacket NmscPacket;
typedef HailstonePet NmscPet;
typedef HailstonePlayer NmscPlayer;
typedef HailstoneServer NmscServer;
typedef HailstoneSpawn NmscSpawn;
typedef HailstoneSpell NmscSpell;
typedef HailstoneTloFn NmscTloFn;
typedef HailstoneUiFn NmscUiFn;
typedef HailstoneValue NmscValue;
typedef HailstoneVitals NmscVitals;
typedef HailstoneWebState NmscWebState;
typedef HailstoneWho NmscWho;
typedef HailstoneWhoEntry NmscWhoEntry;
typedef HailstoneXTarget NmscXTarget;

#define NMSC_API HAILSTONE_API
#define NMSC_BOOL HAILSTONE_BOOL
#define NMSC_CAP_ HAILSTONE_CAP_
#define NMSC_CAP_ACTIONS HAILSTONE_CAP_ACTIONS
#define NMSC_CAP_CHAT HAILSTONE_CAP_CHAT
#define NMSC_CAP_COMMANDS HAILSTONE_CAP_COMMANDS
#define NMSC_CAP_EVENTS HAILSTONE_CAP_EVENTS
#define NMSC_CAP_EXTEND HAILSTONE_CAP_EXTEND
#define NMSC_CAP_LOGIN HAILSTONE_CAP_LOGIN
#define NMSC_CAP_MOVE HAILSTONE_CAP_MOVE
#define NMSC_CAP_NET HAILSTONE_CAP_NET
#define NMSC_CAP_PACKETS HAILSTONE_CAP_PACKETS
#define NMSC_CAP_READ HAILSTONE_CAP_READ
#define NMSC_CAP_TARGET HAILSTONE_CAP_TARGET
#define NMSC_CAP_UI HAILSTONE_CAP_UI
#define NMSC_CAP_WEB HAILSTONE_CAP_WEB
#define NMSC_EXPORT HAILSTONE_EXPORT
#define NMSC_EXTERN_C HAILSTONE_EXTERN_C
#define NMSC_FLAG_ HAILSTONE_FLAG_
#define NMSC_FLAG_AA_ANY_LEVEL HAILSTONE_FLAG_AA_ANY_LEVEL
#define NMSC_FLAG_AA_TRAIN_ALL HAILSTONE_FLAG_AA_TRAIN_ALL
#define NMSC_FLAG_AUGMENT_CLASSES HAILSTONE_FLAG_AUGMENT_CLASSES
#define NMSC_FLAG_BACKSTAB_EITHER_HAND HAILSTONE_FLAG_BACKSTAB_EITHER_HAND
#define NMSC_FLAG_FOCUS_CLASSES HAILSTONE_FLAG_FOCUS_CLASSES
#define NMSC_FLAG_HIDE_ANY_CLASS HAILSTONE_FLAG_HIDE_ANY_CLASS
#define NMSC_FLAG_ITEMS_ANY_CLASS HAILSTONE_FLAG_ITEMS_ANY_CLASS
#define NMSC_FLAG_RESEARCH_CLASSES HAILSTONE_FLAG_RESEARCH_CLASSES
#define NMSC_FLAG_SOLO_ADVENTURE HAILSTONE_FLAG_SOLO_ADVENTURE
#define NMSC_FLAG_WIDE_ITEM_LINKS HAILSTONE_FLAG_WIDE_ITEM_LINKS
#define NMSC_FLOAT HAILSTONE_FLOAT
#define NMSC_HOST_GLOBAL HAILSTONE_HOST_GLOBAL
#define NMSC_HOST_HAS HAILSTONE_HOST_HAS
#define NMSC_IN HAILSTONE_IN
#define NMSC_INT HAILSTONE_INT
#define NMSC_NIL HAILSTONE_NIL
#define NMSC_OUT HAILSTONE_OUT
#define NMSC_PLUGIN HAILSTONE_PLUGIN
#define NMSC_PLUGIN_ABI HAILSTONE_PLUGIN_ABI
#define NMSC_SLOT_CURSOR HAILSTONE_SLOT_CURSOR
#define NMSC_STREAM_WORLD HAILSTONE_STREAM_WORLD
#define NMSC_STREAM_ZONE HAILSTONE_STREAM_ZONE
#define NMSC_STRING HAILSTONE_STRING
#define NMSC_TABLE HAILSTONE_TABLE
#define NMSC_TCP_MAX HAILSTONE_TCP_MAX
#define NMSC_VITAL_ HAILSTONE_VITAL_
#define NMSC_VITAL_CUR_ENDURANCE HAILSTONE_VITAL_CUR_ENDURANCE
#define NMSC_VITAL_CUR_HP HAILSTONE_VITAL_CUR_HP
#define NMSC_VITAL_CUR_MANA HAILSTONE_VITAL_CUR_MANA
#define NMSC_VITAL_MAX_ENDURANCE HAILSTONE_VITAL_MAX_ENDURANCE
#define NMSC_VITAL_MAX_HP HAILSTONE_VITAL_MAX_HP
#define NMSC_VITAL_MAX_MANA HAILSTONE_VITAL_MAX_MANA
#define NMSC_WHO_ENTRY HAILSTONE_WHO_ENTRY

#if !defined(HAILSTONE_HOST) && !defined(NMSC_NO_OLD_GLOBAL)
#define nmsc host
#endif

#endif
