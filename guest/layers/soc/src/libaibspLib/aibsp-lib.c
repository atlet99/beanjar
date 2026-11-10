// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* O22's AI picture tone and scene services for pqcontroller (libaibspLib.so.1), with no AI picture
 * engine behind them. The stock Initialize entries start worker threads that retry /dev/mem and
 * /proc/lg/de/fpp_initialize every 20 ms forever, and W24G's AIBSP_PQ_Enable(0) reads through an
 * unchecked mmap of /dev/mem (SEGV). pqcontroller's mw_pq_initAiPq calls the Initialize entries
 * unconditionally and treats 0 as success everywhere; here every entry it imports returns 0 and
 * starts nothing, so the AI picture path is a no-op. Signatures from pqcontroller's mw_pq_* calls
 * (W22O, W23O, W24G; only W24G imports AIBSP_PQ_Enable). */

int AIBSP_TONE_Initialize(void)
{
    return 0;
}

int AIBSP_TONE_Finalize(void)
{
    return 0;
}

int AIBSP_TONE_RegisterCB(void *callback)
{
    (void)callback;
    return 0;
}

int AIBSP_TONE_UnregisterCB(void)
{
    return 0;
}

int AIBSP_TONE_Enable(int enable)
{
    (void)enable;
    return 0;
}

/* The DTM table and two parameter blocks inside it; the stock entry only reads them. */
int AIBSP_TONE_SetInputParam(const void *dtm_table, const void *param1, const void *param2)
{
    (void)dtm_table;
    (void)param1;
    (void)param2;
    return 0;
}

int AIBSP_SCENE_Initialize(void)
{
    return 0;
}

int AIBSP_SCENE_Finalize(void)
{
    return 0;
}

int AIBSP_SCENE_RegisterCB(void *callback)
{
    (void)callback;
    return 0;
}

int AIBSP_SCENE_UnregisterCB(void)
{
    return 0;
}

int AIBSP_SCENE_Enable(int enable)
{
    (void)enable;
    return 0;
}

int AIBSP_PQ_Enable(int enable)
{
    (void)enable;
    return 0;
}
