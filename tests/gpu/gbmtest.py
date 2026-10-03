# GBM + EGL on a DRM node without KMS: does device creation, buffer allocation, dma-buf export and GL rendering work?
import ctypes as C, os, sys, fcntl, struct, errno
gbm = C.CDLL('libgbm.so.1'); egl = C.CDLL('libEGL.so.1')
for f in ('gbm_create_device', 'gbm_bo_create', 'gbm_bo_create_with_modifiers'): getattr(gbm, f).restype = C.c_void_p
gbm.gbm_device_get_backend_name.restype = C.c_char_p; gbm.gbm_device_get_backend_name.argtypes = [C.c_void_p]
gbm.gbm_bo_create.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_uint, C.c_uint]
gbm.gbm_bo_get_fd.argtypes = [C.c_void_p]; gbm.gbm_bo_get_modifier.argtypes = [C.c_void_p]; gbm.gbm_bo_get_modifier.restype = C.c_uint64
gbm.gbm_bo_destroy.argtypes = [C.c_void_p]; gbm.gbm_device_destroy.argtypes = [C.c_void_p]
gbm.gbm_device_is_format_supported.argtypes = [C.c_void_p, C.c_uint, C.c_uint]
egl.eglGetPlatformDisplay.restype = C.c_void_p; egl.eglGetPlatformDisplay.argtypes = [C.c_uint, C.c_void_p, C.c_void_p]
egl.eglQueryString.restype = C.c_char_p; egl.eglQueryString.argtypes = [C.c_void_p, C.c_int]
egl.eglInitialize.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
egl.eglCreateContext.restype = C.c_void_p; egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglMakeCurrent.argtypes = [C.c_void_p] * 4; egl.eglTerminate.argtypes = [C.c_void_p]
egl.eglChooseConfig.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int, C.POINTER(C.c_int)]
XR24 = 0x34325258; USE_SCANOUT, USE_RENDERING = 1, 4
def kms(fd):  # DRM_IOCTL_MODE_GETRESOURCES: fails with EOPNOTSUPP on a device without DRIVER_MODESET
    try: fcntl.ioctl(fd, 0xC04064A0, bytearray(64)); return 'KMS yes'
    except OSError as e: return 'KMS no (%s)' % errno.errorcode.get(e.errno, e.errno)
for node in sys.argv[1:]:
    try: fd = os.open(node, os.O_RDWR | os.O_CLOEXEC)
    except OSError as e: print(node, 'open failed:', e); continue
    print(node, kms(fd), end=' | ')
    dev = gbm.gbm_create_device(fd)
    if not dev: print('gbm_create_device FAILED'); os.close(fd); continue
    print('gbm backend', gbm.gbm_device_get_backend_name(dev).decode(), end=' | ')
    print('XR24 render ok' if gbm.gbm_device_is_format_supported(dev, XR24, USE_RENDERING) else 'XR24 render unsupported', end=' | ')
    for use, name in ((USE_RENDERING, 'render'), (USE_SCANOUT | USE_RENDERING, 'scanout')):
        bo = gbm.gbm_bo_create(dev, 256, 256, XR24, use)
        if not bo: print('bo(%s) FAILED' % name, end=' | '); continue
        d = gbm.gbm_bo_get_fd(bo)
        print('bo(%s) dmabuf fd %s modifier %#x' % (name, 'ok' if d >= 0 else 'FAILED', gbm.gbm_bo_get_modifier(bo)), end=' | ')
        if d >= 0: os.close(d)
        gbm.gbm_bo_destroy(bo)
    dpy = egl.eglGetPlatformDisplay(0x31D7, dev, None)  # EGL_PLATFORM_GBM_KHR
    ma, mi = C.c_int(), C.c_int()
    if not dpy or not egl.eglInitialize(dpy, C.byref(ma), C.byref(mi)):
        print('EGL init FAILED %#x' % egl.eglGetError())
    else:
        print('EGL %d.%d vendor %s' % (ma.value, mi.value, egl.eglQueryString(dpy, 0x3053).decode()), end=' | ')
        egl.eglBindAPI(0x30A2)  # EGL_OPENGL_API
        cfg, n = C.c_void_p(), C.c_int()
        attrs = (C.c_int * 5)(0x3040, 0x0008, 0x3033, 0, 0x3038)  # RENDERABLE_TYPE OPENGL_BIT, SURFACE_TYPE 0
        egl.eglChooseConfig(dpy, attrs, C.byref(cfg), 1, C.byref(n))
        ctx = egl.eglCreateContext(dpy, cfg if n.value else None, None, None)
        if ctx and egl.eglMakeCurrent(dpy, None, None, ctx):
            gl = C.CDLL('libOpenGL.so.0'); gl.glGetString.restype = C.c_char_p
            print('GL', gl.glGetString(0x1F01).decode(), '/', gl.glGetString(0x1F02).decode())
            egl.eglMakeCurrent(dpy, None, None, None)
        else: print('context FAILED %#x (configs %d)' % (egl.eglGetError(), n.value))
        egl.eglTerminate(dpy)
    gbm.gbm_device_destroy(dev); os.close(fd)
