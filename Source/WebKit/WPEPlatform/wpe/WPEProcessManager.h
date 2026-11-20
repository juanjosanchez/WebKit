/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WPEProcessManager_h
#define WPEProcessManager_h

#if !defined(__WPE_PLATFORM_H_INSIDE__) && !defined(BUILDING_WEBKIT)
#error "Only <wpe/wpe-platform.h> can be included directly."
#endif

#include <glib-object.h>
#include <wpe/WPEDefines.h>

G_BEGIN_DECLS

/**
 * WPEProcessType:
 * @WPE_PROCESS_TYPE_WEB: a Web process.
 * @WPE_PROCESS_TYPE_NETWORK: a Network process.
 * @WPE_PROCESS_TYPE_GPU: a GPU process.
 *
 * The type of an auxiliary process launched by WebKit.
 */
typedef enum {
    WPE_PROCESS_TYPE_WEB,
    WPE_PROCESS_TYPE_NETWORK,
    WPE_PROCESS_TYPE_GPU
} WPEProcessType;

typedef struct _WPEProcessLaunchOptions WPEProcessLaunchOptions;

#define WPE_TYPE_PROCESS_LAUNCH_OPTIONS (wpe_process_launch_options_get_type())

WPE_API GType                    wpe_process_launch_options_get_type          (void);
WPE_API WPEProcessLaunchOptions *wpe_process_launch_options_new               (WPEProcessType            process_type,
                                                                               guint64                   process_id,
                                                                               int                       ipc_socket_fd);
WPE_API WPEProcessLaunchOptions *wpe_process_launch_options_copy              (WPEProcessLaunchOptions  *options);
WPE_API void                     wpe_process_launch_options_free              (WPEProcessLaunchOptions  *options);
WPE_API WPEProcessType           wpe_process_launch_options_get_process_type  (WPEProcessLaunchOptions  *options);
WPE_API guint64                  wpe_process_launch_options_get_process_id    (WPEProcessLaunchOptions  *options);
WPE_API int                      wpe_process_launch_options_get_ipc_socket_fd (WPEProcessLaunchOptions  *options);

#define WPE_PROCESS_MANAGER_ERROR (wpe_process_manager_error_quark())

/**
 * WPEProcessManagerError:
 * @WPE_PROCESS_MANAGER_ERROR_LAUNCH_FAILED: the process could not be launched.
 *
 * [class@ProcessManager] errors.
 */
typedef enum {
    WPE_PROCESS_MANAGER_ERROR_LAUNCH_FAILED
} WPEProcessManagerError;

/**
 * WPEProcessPriority:
 * @WPE_PROCESS_PRIORITY_BACKGROUND: the process has work to finish, such as
 *   parking a page in the back/forward cache or a reply WebKit waits for, but
 *   nothing the user sees depends on it.
 * @WPE_PROCESS_PRIORITY_FOREGROUND: the user sees or hears the process, for example
 *   because it shows a visible page or plays sound, or the application waits on it.
 *
 * How much WebKit needs an auxiliary process to run. See
 * [vfunc@ProcessManager.acquire_priority].
 */
typedef enum {
    WPE_PROCESS_PRIORITY_BACKGROUND,
    WPE_PROCESS_PRIORITY_FOREGROUND
} WPEProcessPriority;

#define WPE_TYPE_PROCESS_MANAGER (wpe_process_manager_get_type())
WPE_DECLARE_DERIVABLE_TYPE (WPEProcessManager, wpe_process_manager, WPE, PROCESS_MANAGER, GObject)

/**
 * WPEProcessManagerClass:
 * @launch: launch an auxiliary process described by a #WPEProcessLaunchOptions.
 *   See [vfunc@ProcessManager.launch] for details about the returned identifier.
 * @terminate: terminate a previously launched process, identified by the value
 *   returned by @launch.
 * @acquire_priority: keep a process running at a #WPEProcessPriority until the
 *   returned hold is released with @release_priority. WebKit calls it on its
 *   main thread, for processes returned by @launch. Holds may overlap, and the
 *   manager returns 0 for a process it does not rank, such as one it does not
 *   know or that already ended. A hold identifier is never 0 and is not given
 *   twice.
 * @release_priority: release a hold returned by @acquire_priority. WebKit calls
 *   it on its main thread, also after the process ended or was terminated, so
 *   the manager ignores holds it no longer has.
 *
 * Subclass this to take over launching and terminating WebKit auxiliary
 * processes. This is required on Android, where processes cannot be spawned as
 * standalone executables and must be created by the embedder instead. The
 * embedder registers its manager as the default with
 * [id@wpe_process_manager_set_default].
 */
struct _WPEProcessManagerClass {
    GObjectClass parent_class;

    /**
     * WPEProcessManagerClass::launch:
     * @manager: a #WPEProcessManager.
     * @options: the #WPEProcessLaunchOptions describing the process to launch.
     * @error: return location for error or %NULL to ignore.
     *
     * The returned value is opaque to WebKit and is passed back to the
     * [vfunc@ProcessManager.terminate] virtual function, so the process manager
     * must be able to map it to the process it launched. It does not need to
     * correspond to any operating system identifier. The Android process manager,
     * for example, uses it in [vfunc@ProcessManager.terminate] to find and unbind
     * the bound service backing the process.
     *
     * Returns: an identifier for the launched process, or 0 and sets @error on
     *   failure.
     */
    guint64 (* launch)    (WPEProcessManager* manager, WPEProcessLaunchOptions* options, GError** error);
    void    (* terminate) (WPEProcessManager* manager, guint64 process_id);

    /**
     * WPEProcessManagerClass::acquire_priority:
     * @manager: a #WPEProcessManager.
     * @process_id: the identifier of the process, as returned by
     *   [vfunc@ProcessManager.launch].
     * @priority: the #WPEProcessPriority to hold.
     *
     * WebKit calls this when it needs a process to run at @priority, and
     * releases the hold with [vfunc@ProcessManager.release_priority] when it no
     * longer does. Holds on the same process may overlap: the process should
     * run at the highest priority it holds. A process with no hold is idle,
     * and the system may suspend it, or end it before others when memory runs
     * low. WebKit takes a hold again before it needs an idle process.
     *
     * A manager that implements this lets WebKit rank the processes it
     * launches; one that does not keeps them all running, as before.
     *
     * Returns: an identifier for the hold, or 0 when the process is not ranked.
     */
    guint64 (* acquire_priority) (WPEProcessManager* manager, guint64 process_id, WPEProcessPriority priority);

    /**
     * WPEProcessManagerClass::release_priority:
     * @manager: a #WPEProcessManager.
     * @priority_id: a hold returned by [vfunc@ProcessManager.acquire_priority].
     *
     * Releases a hold. It may come after the process ended.
     */
    void    (* release_priority) (WPEProcessManager* manager, guint64 priority_id);

    /*< private >*/
    gpointer padding[6];
};

WPE_API GQuark   wpe_process_manager_error_quark      (void);
WPE_API guint64  wpe_process_manager_launch           (WPEProcessManager*       manager,
                                                       WPEProcessLaunchOptions* options,
                                                       GError**                 error);
WPE_API void     wpe_process_manager_terminate        (WPEProcessManager*       manager,
                                                       guint64                  process_id);
WPE_API gboolean wpe_process_manager_ranks_processes  (WPEProcessManager*       manager);
WPE_API guint64  wpe_process_manager_acquire_priority (WPEProcessManager*       manager,
                                                       guint64                  process_id,
                                                       WPEProcessPriority       priority);
WPE_API void     wpe_process_manager_release_priority (WPEProcessManager*       manager,
                                                       guint64                  priority_id);

WPE_API WPEProcessManager *wpe_process_manager_get_default (void);
WPE_API void               wpe_process_manager_set_default (WPEProcessManager*       manager);

G_END_DECLS

#endif /* WPEProcessManager_h */
