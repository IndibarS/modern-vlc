// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * outputs.c: audio/video outputs player test
 *****************************************************************************
 * Copyright (C) 2018-2025 VLC authors and VideoLAN
 *****************************************************************************/

#include "common.h"
#include <vlc_vout_osd.h>

static void
volume_report(audio_output_t *aout, float volume, void *data)
{
    (void) aout;
    assert(volume == 1.0f || volume == 2.0f);
    unsigned *count = data;
    ++*count;
}

static void
test_initial_volume(struct ctx *ctx)
{
    audio_output_t *aout = vlc_player_aout_Hold(ctx->player);
    assert(aout);
    const float saved = var_GetFloat(aout, "volume");
    var_SetFloat(aout, "volume", -1.f);
    unsigned reports = 0;
    const struct vlc_player_aout_cbs cbs = { .on_volume_changed = volume_report };
    vlc_player_aout_listener_id *listener =
        vlc_player_aout_AddListener(ctx->player, &cbs, &reports);
    assert(listener);
    /* Simulate the backend's first valid volume and subsequent updates. */
    var_SetFloat(aout, "volume", 1.f);
    assert(reports == 1);
    var_SetFloat(aout, "volume", 1.f);
    assert(reports == 1);
    var_SetFloat(aout, "volume", 2.f);
    assert(reports == 2);
    var_SetFloat(aout, "volume", -1.f);
    assert(reports == 2);
    vlc_player_aout_RemoveListener(ctx->player, listener);
    var_SetFloat(aout, "volume", saved);
    aout_Release(aout);
}

static int
osd_report(vlc_object_t *obj, const char *name, vlc_value_t old,
           vlc_value_t value, void *data)
{
    (void) obj; (void) name; (void) old;
    struct vlc_osd_event *event = value.p_address;
    unsigned *count = data;
    switch ((*count)++)
    {
        case 0: case 1:
            assert(event->text && strcmp(event->text, "Volume: 200%") == 0);
            break;
        case 2: assert(event->type == OSD_PAUSE_ICON); break;
        case 3:
            assert(event->type == OSD_VERT_SLIDER && event->value == 200);
            break;
        default: vlc_assert_unreachable();
    }
    event->handled = true;
    return VLC_SUCCESS;
}

static void
test_osd_handoff(struct ctx *ctx)
{
    vout_thread_t *vout = vlc_player_vout_Hold(ctx->player);
    assert(vout);
    unsigned reports = 0;
    const bool saved_osd = var_InheritBool(vout, "osd");
    var_Create(vout, "osd", VLC_VAR_BOOL);
    var_SetBool(vout, "osd", true);
    var_Create(vout, "osd-event", VLC_VAR_ADDRESS | VLC_VAR_ISCOMMAND);
    var_AddCallback(vout, "osd-event", osd_report, &reports);
    vout_OSDMessage(vout, 0, "Volume: %d%%", 200);
    vout_OSDMessage(vout, 0, "Volume: %d%%", 200);
    vout_OSDIcon(vout, 0, OSD_PAUSE_ICON);
    vout_OSDSlider(vout, 0, 200, OSD_VERT_SLIDER);
    assert(reports == 4);
    var_SetBool(vout, "osd", false);
    vout_OSDMessage(vout, 0, "Hidden");
    vout_OSDIcon(vout, 0, OSD_PLAY_ICON);
    assert(reports == 4);
    var_DelCallback(vout, "osd-event", osd_report, &reports);
    var_Destroy(vout, "osd-event");
    var_SetBool(vout, "osd", saved_osd);
    var_Destroy(vout, "osd");
    vout_Release(vout);
}

static void
test_no_outputs(struct ctx *ctx)
{
    test_log("test_no_outputs\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(10));
    player_set_current_mock_media(ctx, "media1", &params, false);
    player_start(ctx);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPING);
    {
        vec_on_vout_changed *vec = &ctx->report.on_vout_changed;
        assert(vec->size == 0);
    }

    audio_output_t *aout = vlc_player_aout_Hold(player);
    assert(!aout);

    test_end(ctx);
}

static void
test_outputs(struct ctx *ctx)
{
    test_log("test_outputs\n");
    vlc_player_t *player = ctx->player;

    /* Test that the player has a valid aout and vout, even before first
     * playback */
    audio_output_t *aout = vlc_player_aout_Hold(player);
    assert(aout);

    vout_thread_t *vout = vlc_player_vout_Hold(player);
    assert(vout);

    size_t vout_count;
    vout_thread_t **vout_list = vlc_player_vout_HoldAll(player, &vout_count);
    assert(vout_count == 1 && vout_list[0] == vout);
    vout_Release(vout_list[0]);
    free(vout_list);
    vout_Release(vout);

    /* Test that the player keep the same aout and vout during playback */
    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(10));

    player_set_current_mock_media(ctx, "media1", &params, false);
    player_start(ctx);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPING);

    {
        vec_on_vout_changed *vec = &ctx->report.on_vout_changed;
        assert(vec->size >= 1);
        assert(vec->data[0].action == VLC_PLAYER_VOUT_STARTED);

        vout_thread_t *same_vout = vlc_player_vout_Hold(player);
        assert(vec->data[0].vout == same_vout);
        vout_Release(same_vout);
    }

    audio_output_t *same_aout = vlc_player_aout_Hold(player);
    assert(same_aout == aout);
    aout_Release(same_aout);

    aout_Release(aout);
    test_end(ctx);
}

int
main(void)
{
    struct ctx ctx;

    /* Test with --aout=none --vout=none */
    ctx_init(&ctx, DISABLE_VIDEO_OUTPUT | DISABLE_AUDIO_OUTPUT);
    test_no_outputs(&ctx);
    ctx_destroy(&ctx);

    /* Test with normal outputs */
    ctx_init(&ctx, 0);
    test_initial_volume(&ctx);
    test_osd_handoff(&ctx);
    test_outputs(&ctx);
    ctx_destroy(&ctx);

    return 0;
}
