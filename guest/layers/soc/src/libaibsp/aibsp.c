// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* O22's AI picture BSP for nn-proxy (libaibsp.so.1), with no NPU behind it. The stock
 * AIBSP_Initialize opens /dev/video241 and /dev/mem and returns -1 without them; NnProxy::Create
 * then skips its message server and the teardown quits a GMainLoop never made (SEGV). Every entry
 * nn-proxy imports succeeds here and touches nothing; the AI upscaling, SQM and genre features
 * report no data. Signatures from nn-proxy's NnDriverImpl calls (W22O, W23O, W24G alike). */

/* AIBSP_GetNnParam's result: nn-proxy reads count and value, then count words of data. */
struct aibsp_nn_param {
    int count;
    int value;
    int data[5];
};

int AIBSP_Initialize(void)
{
    return 0;
}

int AIBSP_Finalize(void)
{
    return 0;
}

int AIBSP_SetNnParam(int id, int value)
{
    (void)id;
    (void)value;
    return 0;
}

int AIBSP_GetNnParam(int id, struct aibsp_nn_param *param)
{
    (void)id;
    if (param) {
        param->count = 0;
        param->value = 0;
    }
    return 0;
}

/* Two notification callbacks, by pointer to the pair. Never called: there is no AI event. */
int AIBSP_RegisterNotifyCB(void *callbacks)
{
    (void)callbacks;
    return 0;
}

int AIBSP_RegisterGenreChangeCB(void *callback, void *user_data)
{
    (void)callback;
    (void)user_data;
    return 0;
}
