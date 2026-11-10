"""Generated from core/core.h by tools/gen-ctypes.py. Do not edit."""
import ctypes

ABI = 16
_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_PCM = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_short), ctypes.c_int)
_P = ctypes.c_void_p
_I = ctypes.c_int
_U = ctypes.c_uint
_S = ctypes.c_char_p


_SIGNATURES = {
    "wv_abi": (_I, []),
    "wv_new": (_P, []),
    "wv_option": (_I, [_P, _S, _S]),
    "wv_callbacks": (None, [_P, _CB, _CB, _CB, _P]),
    "wv_signals": (None, []),
    "wv_audio_sink": (None, [_P, _PCM, _P]),
    "wv_audio_wanted": (None, [_P, _I]),
    "wv_start": (None, [_P]),
    "wv_attach": (None, [_P]),
    "wv_poll": (_I, [_P]),
    "wv_view": (None, [_P, _I, _I]),
    "wv_render": (None, [_P, _I, _I]),
    "wv_picture": (_I, [_P, ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I)]),
    "wv_turn": (None, [_P, _I]),
    "wv_screenshot": (_I, [_P, _S, _I, _I]),
    "wv_screenshot_hook": (None, [_P, _CB, _P]),
    "wv_screenshot_request": (_S, [_P, ctypes.POINTER(_I), ctypes.POINTER(_I)]),
    "wv_screenshot_reply": (None, [_P, _I]),
    "wv_frame": (_I, [_P, _P, _I, _I]),
    "wv_frame_fbo": (_I, [_P, _U, _I, _I]),
    "wv_planes": (_I, [_P, _I]),
    "wv_cursor": (_U, [_P, _P, ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I), ctypes.POINTER(_I)]),
    "wv_key_evdev": (_I, [_P, _U, _I]),
    "wv_ir": (None, [_P, _U, _I]),
    "wv_local_key": (None, [_P, _U, _I]),
    "wv_motion": (None, [_P, _I, _I]),
    "wv_leave": (None, [_P]),
    "wv_button": (None, [_P, _U, _I, _I, _I]),
    "wv_wheel": (None, [_P, _I]),
    "wv_touch": (None, [_P, _U, _U, _I, _I]),
    "wv_release": (None, [_P]),
    "wv_pointer_blank": (_I, [_P]),
    "wv_pointer_blank_set": (None, [_P]),
    "wv_standby": (None, [_P, _I]),
    "wv_idle": (None, [_P, _I]),
    "wv_counter": (ctypes.c_long, [_P, _S]),
    "wv_finish": (_I, [_P, _S]),
}
