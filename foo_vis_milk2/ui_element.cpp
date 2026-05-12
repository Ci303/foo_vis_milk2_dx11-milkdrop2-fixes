/*
 * ui_element.cpp - Implements the MilkDrop 2 visualization component.
 *
 * Copyright (c) 2023-2024 Jimmy Cassis
 * SPDX-License-Identifier: MPL-2.0
 */

#include "pch.h"
#include "ui_element.h"

#pragma hdrstop

#ifdef __AVX__
#error `/arch:AVX`, `/arch:AVX2` or `/arch:AVX512` compiler flag set.
#endif

//#ifdef __clang__
//#pragma clang diagnostic ignored "-Wcovered-switch-default"
//#pragma clang diagnostic ignored "-Wswitch-enum"
//#endif

CPlugin g_plugin;
HWND g_hWindow;

// Indicates to hybrid graphics systems to prefer the discrete part by default.
//extern "C" {
//    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
//    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
//}

namespace
{
uint32_t normalize_frame_limit(uint32_t max_fps) noexcept
{
    for (size_t i = 0; i < supported_max_fps_count; ++i)
    {
        if (max_fps == supported_max_fps_values[i])
            return max_fps;
    }
    return default_max_fps_fs;
}

DWORD get_refresh_interval_ms(uint32_t max_fps) noexcept
{
    max_fps = normalize_frame_limit(max_fps);

    return (std::max)(1L, lround(1000.0 / static_cast<double>(max_fps)));
}

bool call_winmm_timer_period(const char* procName) noexcept
{
    using TimerPeriodProc = UINT(WINAPI*)(UINT);
    static HMODULE winmm = ::LoadLibraryW(L"winmm.dll");
    if (winmm == nullptr)
        return false;

    auto proc = reinterpret_cast<TimerPeriodProc>(::GetProcAddress(winmm, procName));
    return proc != nullptr && proc(1) == 0;
}

void normalize_render_size(int& width, int& height) noexcept
{
    if (width < 128)
        width = 128;
    if (height < 128)
        height = 128;
}

void log_runtime_exception(const char* where, const std::exception& exc) noexcept
{
    FB2K_console_print(core_api::get_my_file_name(), ": ", where, " failed - ", exc.what());
}

void log_runtime_exception(const char* where) noexcept
{
    FB2K_console_print(core_api::get_my_file_name(), ": ", where, " failed - unknown exception");
}

std::wstring normalize_playlist_display_text(std::wstring_view input)
{
    std::wstring output;
    output.reserve(input.size());

    bool previous_was_space = false;
    for (wchar_t ch : input)
    {
        const bool whitespace = ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L' ';
        if (whitespace)
        {
            if (!output.empty() && !previous_was_space)
            {
                output.push_back(L' ');
                previous_was_space = true;
            }
            continue;
        }

        output.push_back(ch);
        previous_was_space = false;
    }

    while (!output.empty() && output.back() == L' ')
        output.pop_back();

    return output;
}

template <typename Callback>
bool run_plugin_locked(const char* where, Callback&& callback, bool wait_for_lock = false) noexcept
{
#ifdef TIMER_TP
    constexpr DWORD max_blocking_lock_wait_ms = 250;
    bool lock_held = false;
#else
    UNREFERENCED_PARAMETER(wait_for_lock);
#endif
    bool callback_attempted = false;
    try
    {
#ifdef TIMER_TP
        if (wait_for_lock)
        {
            const DWORD start = GetTickCount();
            while (TryEnterCriticalSection(&s_cs) == 0)
            {
                if (GetTickCount() - start >= max_blocking_lock_wait_ms)
                {
                    MILK2_CONSOLE_LOG(where, " skipped because render lock stayed busy")
                    return false;
                }
                Sleep(1);
            }
        }
        else if (TryEnterCriticalSection(&s_cs) == 0)
        {
            MILK2_CONSOLE_LOG(where, " skipped because render lock is busy")
            return false;
        }
        lock_held = true;
#endif
        callback_attempted = true;
        callback();
    }
    catch (const std::exception& exc)
    {
        log_runtime_exception(where, exc);
    }
    catch (...)
    {
        log_runtime_exception(where);
    }
#ifdef TIMER_TP
    if (lock_held)
        LeaveCriticalSection(&s_cs);
#endif
    return callback_attempted;
}

static wchar_t s_crash_log_dir[MAX_PATH]{};
static LPTOP_LEVEL_EXCEPTION_FILTER s_previous_crash_filter = nullptr;
static bool s_crash_filter_installed = false;
static volatile LONG s_crash_log_written = 0;

void append_crash_line(HANDLE file, const wchar_t* text) noexcept
{
    if (file == INVALID_HANDLE_VALUE || !text)
        return;

    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(wcslen(text) * sizeof(wchar_t)), &written, nullptr);
    WriteFile(file, L"\r\n", 2 * sizeof(wchar_t), &written, nullptr);
}

LONG CALLBACK milk2_crash_handler(EXCEPTION_POINTERS* exceptionInfo) noexcept
{
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    constexpr DWORD MS_VC_THREAD_NAME_EXCEPTION = 0x406D1388;
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP || code == MS_VC_THREAD_NAME_EXCEPTION)
        return EXCEPTION_CONTINUE_SEARCH;

    if (InterlockedCompareExchange(&s_crash_log_written, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t basePath[MAX_PATH]{};
    swprintf_s(basePath, L"%sfoo_vis_milk2-crash-%04u%02u%02u-%02u%02u%02u-%lu",
               s_crash_log_dir[0] ? s_crash_log_dir : L".\\",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               GetCurrentProcessId());

    wchar_t textPath[MAX_PATH]{};
    swprintf_s(textPath, L"%s.txt", basePath);
    HANDLE textFile = CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (textFile != INVALID_HANDLE_VALUE)
    {
        constexpr wchar_t bom = 0xFEFF;
        DWORD written = 0;
        WriteFile(textFile, &bom, sizeof(bom), &written, nullptr);

        wchar_t line[1024]{};
        swprintf_s(line, L"foo_vis_milk2 crash diagnostic");
        append_crash_line(textFile, line);
        swprintf_s(line, L"ExceptionCode=0x%08lX ExceptionAddress=0x%p ThreadId=%lu ProcessId=%lu",
                   code,
                   exceptionInfo->ExceptionRecord->ExceptionAddress,
                   GetCurrentThreadId(),
                   GetCurrentProcessId());
        append_crash_line(textFile, line);
        swprintf_s(line, L"CurrentPreset=%ls", g_plugin.m_szCurrentPresetFile);
        append_crash_line(textFile, line);
        swprintf_s(line, L"LoadingPreset=%ls", g_plugin.m_szLoadingPreset);
        append_crash_line(textFile, line);
        swprintf_s(line, L"Frame=%d ScreenMode=%d UiMode=%d LoadingState=%d Presets=%d Dirs=%d CurrentPresetIndex=%d",
                   g_plugin.GetFrame(),
                   g_plugin.GetScreenMode(),
                   static_cast<int>(g_plugin.m_UI_mode),
                   g_plugin.m_nLoadingPreset,
                   g_plugin.m_nPresets,
                   g_plugin.m_nDirs,
                   g_plugin.m_nCurrentPreset);
        append_crash_line(textFile, line);
        swprintf_s(line, L"Text log path: %ls", textPath);
        append_crash_line(textFile, line);
        CloseHandle(textFile);
    }

    wchar_t dumpPath[MAX_PATH]{};
    swprintf_s(dumpPath, L"%s.dmp", basePath);
    HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile != INVALID_HANDLE_VALUE)
    {
        HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        if (dbghelp)
        {
            using MiniDumpWriteDumpProc = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                                       PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
            auto writeDump = reinterpret_cast<MiniDumpWriteDumpProc>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
            if (writeDump)
            {
                MINIDUMP_EXCEPTION_INFORMATION dumpException{};
                dumpException.ThreadId = GetCurrentThreadId();
                dumpException.ExceptionPointers = exceptionInfo;
                dumpException.ClientPointers = FALSE;
                writeDump(GetCurrentProcess(),
                          GetCurrentProcessId(),
                          dumpFile,
                          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
                          &dumpException,
                          nullptr,
                          nullptr);
            }
            FreeLibrary(dbghelp);
        }
        CloseHandle(dumpFile);
    }

    if (s_previous_crash_filter)
        return s_previous_crash_filter(exceptionInfo);

    return EXCEPTION_CONTINUE_SEARCH;
}

void install_crash_logging() noexcept
{
    if (s_crash_filter_installed)
        return;

    try
    {
        pfc::string8 crashPath = filesystem::g_get_native_path(core_api::get_profile_path());
        crashPath.end_with_slash();
        crashPath.add_string("milkdrop2\\crashlogs\\");
        pfc::stringcvt::convert_utf8_to_wide(s_crash_log_dir, std::size(s_crash_log_dir), crashPath, crashPath.length());
        CreateDirectoryW(s_crash_log_dir, nullptr);
    }
    catch (...)
    {
        wcscpy_s(s_crash_log_dir, L".\\");
    }

    s_previous_crash_filter = SetUnhandledExceptionFilter(milk2_crash_handler);
    s_crash_filter_installed = true;
}

milk2_ui_element::milk2_ui_element(ui_element_config::ptr config, ui_element_instance_callback_ptr p_callback) :
    m_callback(p_callback),
    m_bMsgHandled(TRUE),
    play_callback_impl_base(flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_pause),
    playlist_callback_impl_base(flag_on_items_added | flag_on_items_reordered | flag_on_items_removed | flag_on_items_selection_change |
                                flag_on_item_focus_change | flag_on_items_modified | flag_on_playlist_activate | flag_on_playlists_reorder |
                                flag_on_playlists_removed | flag_on_playback_order_changed)
{
    m_milk2 = false;
    m_in_sizemove = false;
    m_in_suspend = false;
    m_minimized = false;
    m_focus_hotkeys_registered = false;
    m_pending_single_click = false;
    m_popout_drag_candidate = false;
    m_render_due_from_timer = false;
    m_click_pause_confirmation_required = false;
    m_click_pause_confirmation_pending = false;
    m_blacklist_load_retries = 0;
    m_last_left_double_click_tick = 0;
    m_click_pause_confirmation_tick = 0;
    m_pending_animated_text_kind = pending_animated_text_kind::none;
    m_pending_animated_status_duration = 1.6f;
    m_pending_animated_status_fade_time = 0.35f;
    m_pending_animated_status_font = SONGTITLE_FONT;
    m_last_time = 0.0;
#if defined(TIMER_TP)
    m_framePacerThread = nullptr;
    m_framePacerStopEvent = nullptr;
    m_framePacerWakeEvent = nullptr;
    LARGE_INTEGER frameTimerFrequency{};
    m_frameTimerFrequencyQpc = QueryPerformanceFrequency(&frameTimerFrequency) ? frameTimerFrequency.QuadPart : 0;
    if (m_frameTimerFrequencyQpc > 0)
        m_frameIntervalQpc = (std::max)(1LL, (m_frameTimerFrequencyQpc + default_max_fps_fs / 2) / default_max_fps_fs);
    else
        m_frameIntervalQpc = 0;
    m_nextFrameQpc = 0;
    m_renderPending = false;
    m_renderPostTick = 0;
    m_timerResolutionActive = false;
#elif defined(TIMER_32)
    m_last_refresh = 0;
#endif
    m_refresh_interval = get_refresh_interval_ms(default_max_fps_fs);
    m_art_data = std::make_unique<artFetchData>();
    m_art_data->size = sizeof(artFetchData);

    m_pwd = L".\\";
    set_configuration(config);
}

ui_element_config::ptr milk2_ui_element::g_get_default_configuration()
{
    MILK2_CONSOLE_LOG("g_get_default_configuration")

    try
    {
        ui_element_config_builder builder;
        milk2_config config;
        config.init();
        config.build(builder, false);
        return builder.finish(g_get_guid());
    }
    catch (exception_io& exc)
    {
        FB2K_console_print(core_api::get_my_file_name(), ": Exception while building default configuration data - ", exc);
        return ui_element_config::g_create_empty(g_get_guid());
    }
}

void milk2_ui_element::set_configuration(ui_element_config::ptr p_data)
{
    MILK2_CONSOLE_LOG("set_configuration")

    //LPVOID dataptr = const_cast<LPVOID>(p_data->get_data());
    //if (dataptr && p_data->get_data_size() >= 4 && static_cast<DWORD*>(dataptr)[0] == ('M' | 'I' << 8 | 'L' << 16 | 'K' << 24))
    //    s_config = p_data;
    //else
    //    s_config = g_get_default_configuration();

    if (s_milk2)
        return;

    s_config.init();
    ui_element_config_parser parser(p_data);
    s_config.parse(parser);
}

ui_element_config::ptr milk2_ui_element::get_configuration()
{
    MILK2_CONSOLE_LOG("get_configuration")

    ui_element_config_builder builder;
    s_config.build(builder, !s_in_toggle);
    return builder.finish(g_get_guid());
}

void milk2_ui_element::notify(const GUID& p_what, size_t p_param1, const void* p_param2, size_t p_param2size)
{
    UNREFERENCED_PARAMETER(p_param1);
    UNREFERENCED_PARAMETER(p_param2);
    UNREFERENCED_PARAMETER(p_param2size);

    MILK2_CONSOLE_LOG("notify")
    if (p_what == ui_element_notify_colors_changed || p_what == ui_element_notify_font_changed)
        Invalidate();
}

HWND GetRealParent(HWND hWnd)
{
    HWND hWndOwner;

    // To obtain a window's owner window, instead of using `GetParent()`,
    // use `GetWindow()` with the `GW_OWNER` flag.
    if (NULL != (hWndOwner = GetWindow(hWnd, GW_OWNER)))
        return hWndOwner;

    // Obtain the parent window and not the owner.
    return GetAncestor(hWnd, GA_PARENT);
}

int milk2_ui_element::OnCreate(LPCREATESTRUCT cs)
{
#ifndef _DEBUG
    UNREFERENCED_PARAMETER(cs);
#endif

    MILK2_CONSOLE_LOG("OnCreate0 ", cs->x, ", ", cs->y, ", ", GetWnd())
    if (!XMVerifyCPUSupport()) {
        FB2K_console_print(core_api::get_my_file_name(), ": CPU does not support mathematics intrinsics. Exiting.");
        return E_FAIL;
    }

    if (!s_milk2)
    {
        ResolvePwd();
        s_config.init();
#ifdef TIMER_TP
        InitializeCriticalSection(&s_cs);
#endif
    }

    if (!m_milk2)
    {
        SetPwd(s_pwd);

        try
        {
            static_api_ptr_t<visualisation_manager> vis_manager;

            vis_manager->create_stream(m_vis_stream, 0);

            m_vis_stream->request_backlog(0.8);
            UpdateChannelMode();
        }
        catch (std::exception& exc)
        {
            FB2K_console_print(core_api::get_my_file_name(), ": Exception while creating visualization stream - ", exc);
        }

#if defined(TIMER_DX)
        message_loop_v2::get()->add_idle_handler(this);
#endif

        RegisterForArtwork();
    }

    HRESULT hr = S_OK;
    int w, h;
    GetDefaultSize(w, h);

    CRect r{};
    WIN32_OP_D(GetClientRect(&r))
    if (r.right - r.left > 0 && r.bottom - r.top > 0)
    {
        w = r.right - r.left;
        h = r.bottom - r.top;
    }
    if (!Initialize(get_wnd(), w, h))
    {
        FB2K_console_print(core_api::get_my_file_name(), ": Could not initialize MilkDrop");
        return -1;
    }
#ifdef TIMER_TP
    else
    {
        StartTimer();
    }
#elif defined(TIMER_32)
    else
    {
        RestartRefreshTimer();
    }
#endif
    MILK2_CONSOLE_LOG("OnCreate1 ", r.right, ", ", r.left, ", ", r.top, ", ", r.bottom, ", ", GetWnd())

    return hr;
}

void milk2_ui_element::OnClose()
{
    MILK2_CONSOLE_LOG("OnClose ", GetWnd())
    if (s_popout)
    {
        ReturnPopoutToPanel();
        return;
    }
    //DestroyWindow();
}

void milk2_ui_element::OnDestroy()
{
    MILK2_CONSOLE_LOG("OnDestroy ", GetWnd())
    UnregisterFocusHotkeys();
#if defined(TIMER_TP)
    m_renderPending = false;
    m_renderPostTick = 0;
    StopTimer();
    EnterCriticalSection(&s_cs);
    bool delete_cs = false;
#elif defined(TIMER_32)
    KillTimer(ID_REFRESH_TIMER);
#elif defined(TIMER_DX)
    message_loop_v2::get()->remove_idle_handler(this);
#endif
    if (m_vis_stream.is_valid())
        m_vis_stream.release();
    //DestroyMenu();
    auto manager = now_playing_album_art_notify_manager::tryGet();
    if (manager.is_valid())
        manager->remove(this);

    if (m_milk2)
        m_milk2 = false;
    s_count = 0ull;

    if (!s_in_toggle)
    {
        MILK2_CONSOLE_LOG("ExitVis")
        s_fullscreen = false;
        s_in_toggle = false;
        s_was_topmost = false;
        s_popout = false;
        s_popout_fullscreen = false;
        s_popout_parent = nullptr;
        s_milk2 = false;
#ifdef TIMER_TP
        delete_cs = true;
#endif
        wcscpy_s(s_config.settings.m_szPresetDir, g_plugin.GetPresetDir()); // save last "Load Preset" menu directory
        g_plugin.PluginQuit();

        HWND parent = GetRealParent(get_wnd());
        if (parent && parent != ::GetDesktopWindow())
        {
            ::SetClassLongPtr(parent, GCLP_HICON, NULL);
            ::SetClassLongPtr(parent, GCLP_HICONSM, NULL);
        }
    }
    else
    {
        s_in_toggle = false;
        if (!s_fullscreen && g_hWindow && g_hWindow != get_wnd() && ::IsWindow(g_hWindow))
        {
            ::PostMessage(g_hWindow, WM_MILK2_RESTORE_WINDOWED, 0, 0);
        }
    }
#ifdef TIMER_TP
    LeaveCriticalSection(&s_cs);
    if (delete_cs)
        DeleteCriticalSection(&s_cs);
#endif
}

void milk2_ui_element::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == ID_CLICK_TIMER)
    {
        KillTimer(ID_CLICK_TIMER);
        if (m_pending_single_click)
        {
            m_pending_single_click = false;
            const DWORD now = GetTickCount();
            if (m_last_left_double_click_tick != 0 && now - m_last_left_double_click_tick <= GetDoubleClickTime())
                return;

            TogglePlaybackFromClick();
        }
        return;
    }

    if (nIDEvent == ID_BLACKLIST_TIMER)
    {
        KillTimer(ID_BLACKLIST_TIMER);
        if (!run_plugin_locked("BlacklistCurrentPreset load replacement", [&] { g_plugin.LoadRandomPreset(0.0f); }, true))
        {
            if (++m_blacklist_load_retries <= ID_BLACKLIST_MAX_RETRIES)
                SetTimer(ID_BLACKLIST_TIMER, ID_BLACKLIST_RETRY_DELAY_MS, nullptr);
            else
                m_blacklist_load_retries = 0;
            return;
        }
        m_blacklist_load_retries = 0;
        return;
    }

    MILK2_CONSOLE_LOG_LIMIT("OnTimer ", GetWnd())
#ifdef TIMER_32
    if (nIDEvent == ID_REFRESH_TIMER)
    {
        KillTimer(ID_REFRESH_TIMER);
        m_render_due_from_timer = true;
        InvalidateRect(NULL, TRUE);
    }
#endif
}

void milk2_ui_element::OnPaint(CDCHandle dc)
{
    UNREFERENCED_PARAMETER(dc);

    MILK2_CONSOLE_LOG_LIMIT("OnPaint ", GetWnd())
    if (m_in_sizemove && m_milk2) // foobar2000 does not enter/exit size/move
    {
        Tick();
    }
    else
    {
        PAINTSTRUCT ps;
        std::ignore = BeginPaint(&ps);
        EndPaint(&ps);
    }
#ifdef TIMER_TP
    if (m_milk2)
        StartTimer();
#endif
    ValidateRect(NULL);
#ifdef TIMER_32
    ULONGLONG now = GetTickCount64();
#endif
    if (m_vis_stream.is_valid())
    {
        BuildWaves();
#ifdef TIMER_32
        ULONGLONG next_refresh = m_last_refresh + m_refresh_interval;
        // (next_refresh < now) would break when GetTickCount() overflows
        if (static_cast<LONGLONG>(next_refresh - now) < 0)
        {
            next_refresh = now;
        }
        SetTimer(ID_REFRESH_TIMER, static_cast<UINT>(next_refresh - now));
#endif
    }
#ifdef TIMER_32
    m_last_refresh = now;
#endif
}

BOOL milk2_ui_element::OnEraseBkgnd(CDCHandle dc)
{
    UNREFERENCED_PARAMETER(dc);

    MILK2_CONSOLE_LOG_LIMIT("OnEraseBkgnd ", GetWnd())
    ++s_count;

#if 0
    CRect r;
    WIN32_OP_D(GetClientRect(&r));
    CBrush brush;
    //const COLORREF rgbBlack = 0x00000000;
    WIN32_OP_D(brush.CreateSolidBrush(m_callback->query_std_color(ui_color_background)) != NULL);
    WIN32_OP_D(dc.FillRect(&r, brush));
#else
    if (!m_milk2 && !s_fullscreen && s_milk2 && !s_in_toggle)
    {
        int w, h;
        GetDefaultSize(w, h);

        CRect r{};
        WIN32_OP_D(GetClientRect(&r))
        if (r.right - r.left > 0 && r.bottom - r.top > 0)
        {
            w = r.right - r.left;
            h = r.bottom - r.top;
        }
        if (!Initialize(get_wnd(), w, h))
        {
            FB2K_console_print(core_api::get_my_file_name(), ": Could not initialize MilkDrop");
        }
    }
#ifdef TIMER_32
    if (m_render_due_from_timer)
    {
        m_render_due_from_timer = false;
        Tick();
    }
#endif
#endif
#if 0
    FB2K_console_print(m_timer.GetFramesPerSecond(), "FPS");
#endif

    return TRUE;
}

void milk2_ui_element::OnMove(CPoint ptPos)
{
    UNREFERENCED_PARAMETER(ptPos);

    MILK2_CONSOLE_LOG("OnMove ", GetWnd())
    if (m_milk2)
    {
#ifdef TIMER_TP
        bool lock_held = false;
#endif
        try
        {
#ifdef TIMER_TP
            if (TryEnterCriticalSection(&s_cs) == 0)
                return;
            lock_held = true;
#endif
            g_plugin.OnWindowMoved();
        }
        catch (const std::exception& exc)
        {
            log_runtime_exception("OnWindowMoved", exc);
        }
        catch (...)
        {
            log_runtime_exception("OnWindowMoved");
        }
#ifdef TIMER_TP
        if (lock_held)
            LeaveCriticalSection(&s_cs);
#endif
    }
}

void milk2_ui_element::OnSize(UINT nType, CSize size)
{
    MILK2_CONSOLE_LOG("OnSize0 ", nType, ", ", size.cx, ", ", size.cy, ", ", GetWnd())
    if (nType == SIZE_MINIMIZED)
    {
        if (!m_minimized)
        {
            m_minimized = true;
            if (!m_in_suspend && m_milk2)
                OnSuspending();
            m_in_suspend = true;
        }
    }
    else if (m_minimized)
    {
        m_minimized = false;
        if (m_in_suspend && m_milk2)
            OnResuming();
        m_in_suspend = false;
    }
    else if (!m_in_sizemove && m_milk2)
    {
        int width = size.cx;
        int height = size.cy;

        if (!width || !height)
            return;
        normalize_render_size(width, height);
        MILK2_CONSOLE_LOG("OnSize1 ", nType, ", ", size.cx, ", ", size.cy, ", ", GetWnd())
#ifdef TIMER_TP
        bool lock_held = false;
#endif
        try
        {
#ifdef TIMER_TP
            if (TryEnterCriticalSection(&s_cs) == 0)
                return;
            lock_held = true;
#endif
            g_plugin.OnWindowSizeChanged(width, height);
        }
        catch (const std::exception& exc)
        {
            log_runtime_exception("OnWindowSizeChanged", exc);
        }
        catch (...)
        {
            log_runtime_exception("OnWindowSizeChanged");
        }
#ifdef TIMER_TP
        if (lock_held)
            LeaveCriticalSection(&s_cs);
#endif
    }
}

void milk2_ui_element::OnEnterSizeMove()
{
    MILK2_CONSOLE_LOG("OnEnterSizeMove ", GetWnd())
    m_in_sizemove = true;
}

void milk2_ui_element::OnExitSizeMove()
{
    MILK2_CONSOLE_LOG("OnExitSizeMove ", GetWnd())
    m_in_sizemove = false;
    if (m_milk2)
    {
#ifdef TIMER_TP
        bool lock_held = false;
#endif
        RECT rc{};
        WIN32_OP_D(GetClientRect(&rc));
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        if (!width || !height)
            return;
        normalize_render_size(width, height);
        try
        {
#ifdef TIMER_TP
            if (TryEnterCriticalSection(&s_cs) == 0)
                return;
            lock_held = true;
#endif
            g_plugin.OnWindowSizeChanged(width, height);
        }
        catch (const std::exception& exc)
        {
            log_runtime_exception("OnExitSizeMove resize", exc);
        }
        catch (...)
        {
            log_runtime_exception("OnExitSizeMove resize");
        }
#ifdef TIMER_TP
        if (lock_held)
            LeaveCriticalSection(&s_cs);
#endif
    }
}

LRESULT milk2_ui_element::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(wParam);

    if (!s_popout || s_popout_fullscreen || !s_config.settings.m_bPopoutBorderless)
    {
        SetMsgHandled(FALSE);
        return 0;
    }

    RECT windowRect{};
    if (!::GetWindowRect(get_wnd(), &windowRect))
    {
        SetMsgHandled(FALSE);
        return 0;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const int resizeBorder = 8;
    const int dragBand = 40;

    const bool left = point.x >= windowRect.left && point.x < windowRect.left + resizeBorder;
    const bool right = point.x <= windowRect.right && point.x > windowRect.right - resizeBorder;
    const bool top = point.y >= windowRect.top && point.y < windowRect.top + resizeBorder;
    const bool bottom = point.y <= windowRect.bottom && point.y > windowRect.bottom - resizeBorder;

    if (top && left)
        return HTTOPLEFT;
    if (top && right)
        return HTTOPRIGHT;
    if (bottom && left)
        return HTBOTTOMLEFT;
    if (bottom && right)
        return HTBOTTOMRIGHT;
    if (left)
        return HTLEFT;
    if (right)
        return HTRIGHT;
    if (top)
        return HTTOP;
    if (bottom)
        return HTBOTTOM;

    if (point.y >= windowRect.top && point.y < windowRect.top + dragBand)
        return HTCAPTION;

    return HTCLIENT;
}

LRESULT milk2_ui_element::OnNcLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(lParam);

    if (s_popout && !s_popout_fullscreen && s_config.settings.m_bPopoutBorderless && wParam == HTCAPTION)
    {
        TogglePopoutFullscreen();
        return 0;
    }

    SetMsgHandled(FALSE);
    return 0;
}

// To avoid a 1-pixel-thick border of noise.
LRESULT milk2_ui_element::OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    MILK2_CONSOLE_LOG("OnNcCalcSize ", GetWnd())
    if (s_popout && !s_popout_fullscreen && !s_config.settings.m_bPopoutBorderless)
    {
        SetMsgHandled(FALSE);
        return 0;
    }
    if (bCalcValidRects == TRUE)
    {
        //auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        //adjust_fullscreen_client_rect(get_wnd(), params.rgrc[0]);
        return 0;
    }
    return 0;
}

BOOL milk2_ui_element::OnCopyData(CWindow wnd, PCOPYDATASTRUCT pcds)
{
    UNREFERENCED_PARAMETER(wnd);
    //typedef struct
    //{
    //    wchar_t error[1024];
    //} ErrorCopy;

    MILK2_CONSOLE_LOG("OnCopyData ", GetWnd())
    switch (pcds->dwData)
    {
        case 0x09: // PRINT STDOUT
            {
                LPCTSTR lpszString = (LPCTSTR)((ErrorCopy*)(pcds->lpData))->error;
                FB2K_console_print(core_api::get_my_file_name(), ": ", lpszString);
                break;
            }
    }

    return TRUE;
}

void milk2_ui_element::OnDisplayChange(UINT uBitsPerPixel, CSize sizeScreen)
{
    UNREFERENCED_PARAMETER(uBitsPerPixel);
    UNREFERENCED_PARAMETER(sizeScreen);

    MILK2_CONSOLE_LOG("OnDisplayChange ", GetWnd())
    if (m_milk2)
        run_plugin_locked("OnDisplayChange", [] { g_plugin.OnDisplayChange(); });
}

void milk2_ui_element::OnDpiChanged(UINT nDpiX, UINT nDpiY, PRECT pRect)
{
    UNREFERENCED_PARAMETER(nDpiX);
    UNREFERENCED_PARAMETER(nDpiY);
    UNREFERENCED_PARAMETER(pRect);
}

void milk2_ui_element::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
    lpMMI->ptMinTrackSize.x = 150; // 320
    lpMMI->ptMinTrackSize.y = 150; // 200
}

void milk2_ui_element::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
    UNREFERENCED_PARAMETER(dwThreadID);

    MILK2_CONSOLE_LOG("OnActivateApp ", GetWnd())
    if (m_milk2)
    {
        if (bActive)
        {
            OnActivated();
        }
        else
        {
            OnDeactivated();
        }
    }
}

BOOL milk2_ui_element::OnPowerBroadcast(DWORD dwPowerEvent, DWORD_PTR dwData)
{
    UNREFERENCED_PARAMETER(dwData);

    MILK2_CONSOLE_LOG("OnPowerBroadcast ", GetWnd())
    switch (dwPowerEvent)
    {
        case PBT_APMQUERYSUSPEND:
            if (!m_in_suspend && m_milk2)
                OnSuspending();
            m_in_suspend = true;
            break;
        case PBT_APMRESUMESUSPEND:
            if (!m_minimized)
            {
                if (m_in_suspend && m_milk2)
                    OnResuming();
                m_in_suspend = false;
            }
            break;
        default:
            break;
    }

    return TRUE;
}

void milk2_ui_element::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    UNREFERENCED_PARAMETER(point);

    KillTimer(ID_CLICK_TIMER);
    m_pending_single_click = false;
    const DWORD now = GetTickCount();
    m_click_pause_confirmation_required = false;
    m_click_pause_confirmation_pending = false;
    m_last_left_double_click_tick = now;

    MILK2_CONSOLE_LOG("OnLButtonDblClk ", GetWnd())
    ToggleFullScreen();
}

#pragma region Keyboard Controls
#include "keyboard.cpp"
#pragma endregion

void milk2_ui_element::OnContextMenu(CWindow wnd, CPoint point)
{
    UNREFERENCED_PARAMETER(wnd);

    MILK2_CONSOLE_LOG("OnContextMenu ", point.x, ", ", point.y, ", ", GetWnd())
    if (m_callback->is_edit_mode_enabled())
    {
        SetMsgHandled(FALSE);
        return;
    }
    //_context_menu = make_context_menu();

    // A (-1,-1) point is due to context menu key rather than right click.
    // `GetContextMenuPoint()` fixes that, returning a proper point at which the menu should be shown.
    //point = m_list.GetContextMenuPoint(point);
    CMenu menu;
    WIN32_OP_D(menu.CreatePopupMenu()); // ID_VIS_MENU
    //BOOL b = TRUE;
    //CMenu original;
    //b = menu.LoadMenu(IDR_WINDOWED_CONTEXT_MENU);
    //menu.AppendMenu(MF_STRING, menu.GetSubMenu(0), TEXT("Winamp"));
    KillTimer(ID_CLICK_TIMER);
    m_pending_single_click = false;
    m_click_pause_confirmation_required = true;
    m_click_pause_confirmation_pending = false;

    const std::wstring currentPreset = g_plugin.GetCurrentPresetFilename();
    const UINT presetItemFlags = currentPreset.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING;
    menu.AppendMenu(presetItemFlags, IDM_OPEN_PRESET_LOCATION, currentPreset.empty() ? TEXT("(No preset loaded)") : currentPreset.c_str());
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDM_NEXT_PRESET, TEXT("Next Preset"));
    menu.AppendMenu(MF_STRING, IDM_PREVIOUS_PRESET, TEXT("Previous Preset"));
    menu.AppendMenu(MF_STRING, IDM_SHUFFLE_PRESET, TEXT("Shuffle Preset"));
    menu.AppendMenu(MF_STRING | (IsPresetLock() ? MF_CHECKED : 0), IDM_LOCK_PRESET, TEXT("Lock Preset"));
    menu.AppendMenu(currentPreset.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING, IDM_BLACKLIST_PRESET, TEXT("Never Show Again"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (s_config.settings.m_bEnableDownmix ? MF_CHECKED : 0), IDM_ENABLE_DOWNMIX, TEXT("Downmix Channels"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (g_plugin.m_show_playlist ? MF_CHECKED : 0), IDM_SHOW_PLAYLIST, TEXT("Show Playlist"));
    //menu.AppendMenu(MF_STRING | (g_plugin.m_show_presets ? MF_CHECKED : 0), IDM_SHOW_PRESETS, TEXT("Show Presets"));
    //menu.AppendMenu(MF_STRING | (g_plugin.m_show_menu ? MF_CHECKED : 0), IDM_SHOW_MENU, TEXT("Show Menu"));
    menu.AppendMenu(MF_STRING | (s_config.settings.m_bShowAlbum ? MF_CHECKED : 0), IDM_SHOW_ALBUM, TEXT("Show Album Art"));
    menu.AppendMenu(MF_STRING, IDM_SHOW_TITLE, TEXT("Show Track Title"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (g_plugin.m_show_help ? MF_CHECKED : 0), IDM_SHOW_HELP, TEXT("Show Help Overlay"));
    menu.AppendMenu(MF_STRING, IDM_SHOW_PREFS, TEXT("Open Preferences"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (s_popout ? MF_CHECKED : 0), IDM_TOGGLE_POPOUT, s_popout ? TEXT("Return to Panel") : TEXT("Pop Out Window"));
    menu.AppendMenu(MF_STRING | ((s_fullscreen || s_popout_fullscreen) ? MF_CHECKED : 0), IDM_TOGGLE_FULLSCREEN, TEXT("Fullscreen"));

    //auto submenu = std::make_unique<CMenu>(menu.GetSubMenu(0));
    //b = menu.RemoveMenu(0, MF_BYPOSITION);
    //return submenu;
    //int cmd = menu.GetSubMenu(0).TrackPopupMenu(TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, point.x, point.y, *this);
    int cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, point.x, point.y, *this);

    switch (cmd)
    {
        case IDM_OPEN_PRESET_LOCATION:
            OpenCurrentPresetLocation();
            break;
        case IDM_TOGGLE_FULLSCREEN:
            if (g_plugin.GetFrame() > 0)
                ToggleFullScreen();
            break;
        case IDM_TOGGLE_POPOUT:
            TogglePopout();
            break;
        case IDM_NEXT_PRESET:
            NextPreset();
            break;
        case IDM_PREVIOUS_PRESET:
            PrevPreset();
            break;
        case IDM_LOCK_PRESET:
            LockPreset(!IsPresetLock());
            break;
        case IDM_BLACKLIST_PRESET:
            BlacklistCurrentPreset();
            break;
        case IDM_SHUFFLE_PRESET:
            RandomPreset();
            break;
        case IDM_ENABLE_DOWNMIX:
            s_config.settings.m_bEnableDownmix = !s_config.settings.m_bEnableDownmix;
            s_config.persist_runtime_settings();
            UpdateChannelMode();
            break;
        case IDM_SHOW_PREFS:
            ShowPreferencesPage();
            break;
        case IDM_SHOW_HELP:
            ToggleHelp();
            break;
        case IDM_SHOW_PLAYLIST:
            TogglePlaylist();
            break;
        case IDM_SHOW_TITLE:
            LaunchSongTitle();
            break;
        case IDM_SHOW_ALBUM:
            s_config.settings.m_bShowAlbum = !s_config.settings.m_bShowAlbum;
            s_config.persist_runtime_settings();
            ShowAlbumArt(true);
            break;
        case IDM_QUIT:
            //g_plugin.m_exiting = 1;
            //PostMessage(WM_CLOSE, static_cast<WPARAM>(0), static_cast<LPARAM>(0));
            break;
    }

    Invalidate();
}

LRESULT milk2_ui_element::OnImeNotify(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    MILK2_CONSOLE_LOG("OnImeNotify ", GetWnd())
    if (uMsg != WM_IME_NOTIFY)
        return 1;
    if (wParam == IMN_CLOSESTATUSWINDOW)
    {
        if (s_fullscreen && !s_in_toggle)
            ToggleFullScreen();
    }
    return 0;
}

LRESULT milk2_ui_element::OnQuit(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    MILK2_CONSOLE_LOG("OnQuit ", GetWnd())
    return 0;
}

LRESULT milk2_ui_element::OnMilk2Message(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg != WM_MILK2)
        return -1;

    if (wParam == MILK2_WPARAM_REFRESH_PRESET_LIST)
    {
        g_plugin.UpdatePresetList(false, true, true);
        return 0;
    }
    if (wParam == MILK2_WPARAM_RANDOM_PRESET)
    {
        RandomPreset(0.0f);
        return 0;
    }

    auto api = playlist_manager::get();
    if (LOBYTE(wParam) == 0x21 && HIBYTE(wParam) == 0x09)
    {
        wchar_t buf[2048], title[64];
        LoadString(core_api::get_my_instance(), LOWORD(lParam), buf, 2048);
        LoadString(core_api::get_my_instance(), HIWORD(lParam), title, 64);
        MILK2_CONSOLE_LOG("milk2 -> title: ", title, ", message: ", buf)
        return HIWORD(lParam) == IDS_MILKDROP_ERROR || HIWORD(lParam) == IDS_MILKDROP_WARNING ? 0 : 1;
    }
    else if (lParam == IPC_GETVERSION)
    {
        MILK2_CONSOLE_LOG("IPC_GETVERSION")
        //const t_core_version_data v = core_version_info_v2::get_version();
        return 1;
    }
    else if (lParam == IPC_GETVERSIONSTRING)
    {
        MILK2_CONSOLE_LOG("IPC_GETVERSIONSTRING")
        return reinterpret_cast<LRESULT>(core_version_info_v2::g_get_version_string()); // "foobar2000 v2.1.2"
        return 0;
    }
    else if (lParam == IPC_ISPLAYING)
    {
        MILK2_CONSOLE_LOG("IPC_ISPLAYING")
        if (m_playback_control->is_playing())
            return 1;
        else if (m_playback_control->is_paused())
            return 3;
        else
            return 0;
    }
    else if (lParam == IPC_SETPLAYLISTPOS)
    {
        //MILK2_CONSOLE_LOG("IPC_SETPLAYLISTPOS")
        SetSelectionSingle(static_cast<size_t>(g_plugin.m_playlist_pos));
        return static_cast<LRESULT>(wParam);
    }
    else if (lParam == IPC_GETLISTLENGTH)
    {
        //MILK2_CONSOLE_LOG("IPC_GETLISTLENGTH")
        const size_t count = api->activeplaylist_get_item_count();
        return static_cast<LRESULT>(count);
    }
    else if (lParam == IPC_GETLISTPOS)
    {
        //MILK2_CONSOLE_LOG("IPC_GETLISTPOS")
        if (m_playback_control->is_playing() || m_playback_control->is_paused())
        {
            size_t playing_index = NULL, playing_playlist = NULL;
            bool valid = api->get_playing_item_location(&playing_playlist, &playing_index);
            if (valid && playing_playlist == api->get_active_playlist())
                return static_cast<LRESULT>(playing_index);
        }
        return -1;
    }
    else if (lParam == IPC_GETPLAYLISTTITLEW)
    {
        //MILK2_CONSOLE_LOG("IPC_GETPLAYLISTTITLEW")
        static titleformat_object::ptr playlist_script;
        if (playlist_script.is_empty())
        {
            static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(playlist_script, "[$if2(%artist%,%album artist%) - ]%title%");
        }

        pfc::string_formatter state;
        metadb_handle_list list;
        api->activeplaylist_get_all_items(list);

        const size_t index = static_cast<size_t>(wParam);
        const bool use_playing_item = wParam == static_cast<WPARAM>(-1) || index >= list.size();

        if (list.size() == 0)
        {
            state = ""; // no playlist
        }
        else if (!use_playing_item && list.get_item(index)->format_title(NULL, state, playlist_script, NULL) && state.length() > 0)
        {
            // Succeeded already.
        }
        else if (m_playback_control->playback_format_title(NULL, state, playlist_script, NULL, playback_control::display_level_all) && state.length() > 0)
        {
            // Fall back to the active playback item when the active playlist item is unavailable.
        }
        else if (!use_playing_item)
        {
            if (m_script.is_empty())
            {
                pfc::string8 pattern = pfc::utf8FromWide(s_config.settings.m_szTitleFormat);
                static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(m_script, pattern);
            }

            if (list.get_item(index)->format_title(NULL, state, m_script, NULL))
            {
                // Fall back to the user title format if artist/title fields are unavailable.
            }
        }
        else if (m_playback_control->is_playing() || m_playback_control->is_paused())
        {
            state = "Opening...";
        }
        else
        {
            state = "Stopped.";
        }

        m_szBuffer = normalize_playlist_display_text(pfc::wideFromUTF8(state).c_str());
        return reinterpret_cast<LRESULT>(m_szBuffer.c_str());
    }
    else if (lParam == IPC_GET_PLAYING_TITLE)
    {
        //MILK2_CONSOLE_LOG("IPC_GET_PLAYING_TITLE")
        if (m_title.is_empty())
        {
            pfc::string8 pattern = pfc::utf8FromWide(s_config.settings.m_szTitleFormat);
            static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(m_title, pattern);
        }

        pfc::string_formatter state;
        if (m_playback_control->playback_format_title(NULL, state, m_title, NULL, playback_control::display_level_all))
        {
            // Succeeded already.
        }
        else if (m_playback_control->is_playing() || m_playback_control->is_paused())
        {
            state = "Opening...";
        }
        else
        {
            state = "Stopped.";
        }

        m_szBuffer = pfc::wideFromUTF8(state);
        return reinterpret_cast<LRESULT>(m_szBuffer.c_str());
    }
    else if (lParam == IPC_GETOUTPUTTIME)
    {
        //MILK2_CONSOLE_LOG("IPC_GETOUTPUTTIME")
        if (wParam == 0)
        {
            if (m_playback_control->is_playing() || m_playback_control->is_paused())
                return static_cast<LRESULT>(m_playback_control->playback_get_position() * 1000);
        }
        else if (wParam == 1)
        {
            if (m_playback_control->is_playing() || m_playback_control->is_paused())
                return static_cast<LRESULT>(m_playback_control->playback_get_length());
        }
        else if (wParam == 2)
        {
            if (m_playback_control->is_playing() || m_playback_control->is_paused())
                return static_cast<LRESULT>(m_playback_control->playback_get_length() * 1000);
        }
        return -1;
    }
    else if (lParam == IPC_GETPLUGINDIRECTORYW)
    {
        //MILK2_CONSOLE_LOG("IPC_GETPLUGINDIRECTORYW")
        m_szBuffer = s_config.settings.m_szPluginsDirPath;
        return reinterpret_cast<LRESULT>(m_szBuffer.c_str());
    }
    else if (lParam == IPC_GETINIDIRECTORYW)
    {
        //MILK2_CONSOLE_LOG("IPC_GETINIDIRECTORYW")
        m_szBuffer = s_config.settings.m_szConfigIniFile;
        size_t p = m_szBuffer.find_last_of(L"\\");
        if (p != std::wstring::npos)
            m_szBuffer = m_szBuffer.substr(0, p + 1);
        return reinterpret_cast<LRESULT>(m_szBuffer.c_str());
    }
    else if (lParam == IPC_FETCH_ALBUMART)
    {
        MILK2_CONSOLE_LOG("IPC_FETCH_ALBUMART")
        m_art_data->size = sizeof(artFetchData);
        constexpr size_t art_type_capacity = sizeof(m_art_data->type) / sizeof(m_art_data->type[0]);
        std::fill_n(&m_art_data->type[0], art_type_capacity, L'\0');
        if (m_art_file.empty())
        {
            m_art_data->imgData = m_raster.data();
            m_art_data->imgDataLen = static_cast<int>(m_raster.size());
            wcscpy_s(m_art_data->type, L"jpg");
            m_art_data->gracenoteFileId = nullptr;
        }
        else
        {
            m_art_data->imgData = nullptr;
            m_art_data->imgDataLen = 0;
            m_art_data->gracenoteFileId = m_art_file.data();
            std::wstring ext = GetExtension(m_art_file);
            const size_t chars_to_copy = (std::min)(ext.size(), art_type_capacity - 1);
            std::copy_n(ext.begin(), chars_to_copy, &m_art_data->type[0]);
        }
        s_config.settings.m_artData = m_art_data.get();

        return reinterpret_cast<LRESULT>(m_art_data.get());
    }
#if 0
    else if (lParam == IPC_SETVISWND)
    {
        //MILK2_CONSOLE_LOG("IPC_SETVISWND")
        //SendMessage(m_hwnd_winamp, WM_WA_IPC, (WPARAM)m_hwnd, IPC_SETVISWND);
    }
    else if (lParam == IPC_CB_VISRANDOM)
    {
        //MILK2_CONSOLE_LOG("IPC_CB_VISRANDOM")
        //SendMessage(GetWinampWindow(), WM_WA_IPC, (m_bPresetLockOnAtStartup ? 0 : 1) << 16, IPC_CB_VISRANDOM);
    }
    else if (lParam == IPC_IS_PLAYING_VIDEO)
    {
        //MILK2_CONSOLE_LOG("IPC_IS_PLAYING_VIDEO")
        //if (m_screenmode == FULLSCREEN && SendMessage(GetWinampWindow(), WM_WA_IPC, 0, IPC_IS_PLAYING_VIDEO) > 1)
    }
    else if (lParam == IPC_SET_VIS_FS_FLAG)
    {
        //MILK2_CONSOLE_LOG("IPC_SET_VIS_FS_FLAG")
        //SendMessage(GetWinampWindow(), WM_WA_IPC, 1, IPC_SET_VIS_FS_FLAG);
    }
    else if (lParam == IPC_GET_D3DX9)
    {
        //MILK2_CONSOLE_LOG("IPC_GET_D3DX9")
        //HMODULE d3dx9 = (HMODULE)SendMessage(winamp, WM_WA_IPC, 0, IPC_GET_D3DX9);
    }
    else if (lParam == IPC_GET_API_SERVICE)
    {
        //MILK2_CONSOLE_LOG("IPC_GET_API_SERVICE")
        //WASABI_API_SVC = (api_service*)SendMessage(hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE);
    }
    else if (lParam == IPC_GETDIALOGBOXPARENT)
    {
        //MILK2_CONSOLE_LOG("IPC_GETDIALOGBOXPARENT")
        //HWND parent = (HWND)SendMessage(winamp, WM_WA_IPC, 0, IPC_GETDIALOGBOXPARENT);
    }
    else if (lParam == IPC_GET_RANDFUNC)
    {
        //MILK2_CONSOLE_LOG("IPC_GET_RANDFUNC")
        //int warand();
        //warand = (int (*)(void))SendMessage(hwndParent, WM_WA_IPC, 0, IPC_GET_RANDFUNC);
    }
#endif

    return 1;
}

LRESULT milk2_ui_element::OnRenderMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (uMsg != WM_MILK2_RENDER)
        return -1;

#ifdef TIMER_TP
    m_renderPending = false;
    m_renderPostTick = 0;
#endif

    if (!m_milk2)
        return 0;

    LONGLONG renderQpc = 0;
#ifdef TIMER_TP
    if (!TryBeginFrame(renderQpc))
        return 0;
#endif

    BuildWaves();
    Tick();

#ifdef TIMER_TP
    if (m_milk2)
    {
        FinishFrameTiming(renderQpc);
        ScheduleNextFrameTimer();
    }
#endif

    return 0;
}

LRESULT milk2_ui_element::OnRestoreWindowed(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (uMsg != WM_MILK2_RESTORE_WINDOWED)
        return -1;

    if (s_fullscreen || m_milk2)
        return 0;

    int w, h;
    GetDefaultSize(w, h);

    CRect r{};
    if (::GetClientRect(get_wnd(), &r) && r.right - r.left > 0 && r.bottom - r.top > 0)
    {
        w = r.right - r.left;
        h = r.bottom - r.top;
    }

    if (!Initialize(get_wnd(), w, h))
    {
        FB2K_console_print(core_api::get_my_file_name(), ": Could not restore MilkDrop after fullscreen");
        return 0;
    }

#ifdef TIMER_TP
    StartTimer();
    QueueRenderMessage();
#endif
    InvalidateRect(nullptr, FALSE);
    return 0;
}

LRESULT milk2_ui_element::OnConfigurationChange(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    MILK2_CONSOLE_LOG("OnConfigurationChange ", GetWnd())
    if (uMsg != WM_CONFIG_CHANGE)
        return 1;
#ifdef TIMER_TP
    bool lock_held = false;
#endif
    try
    {
        switch (wParam)
        {
            case 0: // Preferences Dialog
            case 1: // Advanced Preferences
                {
#ifdef TIMER_TP
                    EnterCriticalSection(&s_cs);
                    lock_held = true;
#endif
                    s_config.reset();
                    m_script.reset();
                    m_title.reset();
                    g_plugin.PanelSettings(&s_config.settings);
                    ApplyFrameRateLimit();
                    if (s_popout && !s_popout_fullscreen)
                    {
                        const DWORD baseStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
                        const DWORD style = s_config.settings.m_bPopoutBorderless ? (WS_POPUP | baseStyle) : (WS_OVERLAPPEDWINDOW | baseStyle);
                        ::SetWindowLongPtr(get_wnd(), GWL_STYLE, static_cast<LONG_PTR>(style));
                        ::SetWindowLongPtr(get_wnd(), GWL_EXSTYLE, static_cast<LONG_PTR>(WS_EX_APPWINDOW));
                        ::SetWindowPos(get_wnd(), nullptr, 0, 0, 0, 0,
                                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                    }
#ifdef TIMER_TP
                    StartTimer();
#elif defined(TIMER_32)
                    RestartRefreshTimer();
#endif
                    if (s_milk2 && m_milk2)
                    {
                        RECT rect{};
                        GetClientRect(&rect);
                        int width = rect.right - rect.left;
                        int height = rect.bottom - rect.top;
                        if (width > 0 && height > 0)
                        {
                            normalize_render_size(width, height);
                            g_plugin.OnWindowSizeChanged(width, height);
                        }
                    }
                    break;
                }
        }
    }
    catch (const std::exception& exc)
    {
        log_runtime_exception("OnConfigurationChange", exc);
    }
    catch (...)
    {
        log_runtime_exception("OnConfigurationChange");
    }
#ifdef TIMER_TP
    if (lock_held)
        LeaveCriticalSection(&s_cs);
#endif
    SetMsgHandled(TRUE);

    return 0;
}

// Initialize the Direct3D resources required to run.
bool milk2_ui_element::Initialize(HWND window, int width, int height)
{
#ifdef TIMER_TP
    bool lock_held = false;
#endif
    try
    {
        if (!s_fullscreen)
            g_hWindow = get_wnd();
        normalize_render_size(width, height);

        if (!s_milk2)
        {
            if (FALSE == g_plugin.PluginPreInitialize(window, core_api::get_my_instance()))
                return false;
            if (!g_plugin.PanelSettings(&s_config.settings))
                return false;

            //swprintf_s(g_plugin.m_szPluginsDirPath, L"%ls", s_config.settings.m_szPluginsDirPath);
            //swprintf_s(g_plugin.m_szConfigIniFile, L"%ls", s_config.settings.m_szConfigIniFile);
            swprintf_s(g_plugin.m_szComponentDirPath, L"%ls", const_cast<wchar_t*>(m_pwd.c_str()));
        }

        g_plugin.SetWinampWindow(window);
        g_plugin.SetScreenMode((s_fullscreen || s_popout_fullscreen) ? FULLSCREEN : WINDOWED);
        ApplyFrameRateLimit();

        if (!s_milk2)
        {
            if (FALSE == g_plugin.PluginInitialize(width, height))
                return false;
            g_plugin.SetFoobarPlaybackActive(m_playback_control->is_playing() && !m_playback_control->is_paused());

            HICON hIcon = ::LoadIcon(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(IDI_MILK2_ICON));
            HWND parent = GetRealParent(get_wnd());
            ::SetClassLongPtr(parent, GCLP_HICON, (LONG_PTR)hIcon);
            ::SetClassLongPtr(parent, GCLP_HICONSM, (LONG_PTR)hIcon);

            s_milk2 = true;
        }
        else
        {
#ifdef TIMER_TP
            EnterCriticalSection(&s_cs);
            lock_held = true;
#endif
            g_plugin.SetWinampWindow(window);
            g_plugin.SetScreenMode((s_fullscreen || s_popout_fullscreen) ? FULLSCREEN : WINDOWED);
            g_plugin.OnWindowSwap(window, width, height);
#ifdef TIMER_TP
            if (lock_held)
                LeaveCriticalSection(&s_cs);
#endif
        }

        m_milk2 = true;
        return true;
    }
    catch (const std::exception& exc)
    {
#ifdef TIMER_TP
        if (lock_held)
            LeaveCriticalSection(&s_cs);
#endif
        log_runtime_exception("Initialize", exc);
        return false;
    }
    catch (...)
    {
#ifdef TIMER_TP
        if (lock_held)
            LeaveCriticalSection(&s_cs);
#endif
        log_runtime_exception("Initialize");
        return false;
    }
}

#pragma region Frame Update
// Executes the render.
void milk2_ui_element::Tick()
{
#ifdef TIMER_TP
    if (TryEnterCriticalSection(&s_cs) == 0)
        return;
#endif

#ifdef TIMER_DX
    m_timer.Tick([&]() { Update(m_timer); });
#endif

    FlushPendingAnimatedText();

    try
    {
        Render();
    }
    catch (const std::exception& exc)
    {
        log_runtime_exception("Render tick", exc);
    }
    catch (...)
    {
        log_runtime_exception("Render tick");
    }

#ifdef TIMER_TP
    LeaveCriticalSection(&s_cs);

    InvalidateRect(NULL, TRUE);
#endif
}

#ifdef TIMER_DX
// Updates the world.
void milk2_ui_element::Update(DX::StepTimer const& timer)
{
}
#endif
#pragma endregion

#pragma region Frame Render
// Draws the scene.
HRESULT milk2_ui_element::Render()
{
    // Do not try to render anything before the first `Update()`.
#ifdef TIMER_DX
    if (m_timer.GetFrameCount() == 0)
    {
        return S_OK;
    }
#else
    if (g_plugin.GetFrame() == 0)
    {
    }
#endif

    try
    {
        RECT rect{};
        if (m_milk2 && GetClientRect(&rect))
        {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (width > 0 && height > 0)
            {
                normalize_render_size(width, height);
                const int current_width = g_plugin.m_lpDX ? g_plugin.m_lpDX->m_client_width : 0;
                const int current_height = g_plugin.m_lpDX ? g_plugin.m_lpDX->m_client_height : 0;
                if (width != current_width || height != current_height)
                {
                    g_plugin.OnWindowSizeChanged(width, height);
                }
            }
        }

        Clear();
        return g_plugin.PluginRender(waves[0].data(), waves[1].data());
    }
    catch (const std::exception& exc)
    {
        log_runtime_exception("PluginRender", exc);
        return E_FAIL;
    }
    catch (...)
    {
        log_runtime_exception("PluginRender");
        return E_FAIL;
    }
}

// Clears the back buffers and the window contents.
void milk2_ui_element::Clear()
{
#if 0
    HDC hdc = GetDC();
    RECT rect{};
    GetClientRect(&rect);
    FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    ReleaseDC(hdc);
#endif
}

void milk2_ui_element::BuildWaves()
{
    if (!m_vis_stream.is_valid() || !m_playback_control->is_playing() || m_playback_control->is_paused())
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(NUM_AUDIO_BUFFER_SAMPLES); ++i)
            waves[0][i] = waves[1][i] = 0.0f;
        return;
    }

    double time;
    if (!m_vis_stream->get_absolute_time(time))
        return;

    double dt = time - m_last_time;
    m_last_time = time;

    constexpr double min_time = 1.0 / 1000.0;
    constexpr double max_time = 1.0 / 10.0;

    bool use_fake = false;

    if (dt < min_time)
    {
        dt = min_time;
        use_fake = true;
    }
    else if (dt > max_time)
        dt = max_time;

    audio_chunk_impl chunk;
    if (use_fake || !m_vis_stream->get_chunk_absolute(chunk, time - dt, dt))
    {
        //m_vis_stream->make_fake_chunk_absolute(chunk, time - dt, dt);
        for (uint32_t i = 0; i < static_cast<uint32_t>(NUM_AUDIO_BUFFER_SAMPLES); ++i)
        {
            waves[0][i] = waves[1][i] = 0U;
        }
        return;
    }
    auto count = chunk.get_sample_count();
    //auto sample_rate = chunk.get_srate();
    auto channels = chunk.get_channel_count();
    if (channels == 0)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(NUM_AUDIO_BUFFER_SAMPLES); ++i)
            waves[0][i] = waves[1][i] = 0.0f;
        return;
    }
    audio_sample* audio_data = chunk.get_data();

    size_t top = std::min(count / channels, static_cast<size_t>(NUM_AUDIO_BUFFER_SAMPLES));
    if (!audio_data || top == 0)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(NUM_AUDIO_BUFFER_SAMPLES); ++i)
            waves[0][i] = waves[1][i] = 0.0f;
        return;
    }
    for (size_t i = 0; i < top; ++i)
    {
        waves[0][i] = static_cast<float>(audio_data[i * channels] * 128.0f);
        if (channels >= 2)
            waves[1][i] = static_cast<float>(audio_data[i * channels + 1] * 128.0f);
        else
            waves[1][i] = waves[0][i];
    }
    for (size_t i = top; i < static_cast<size_t>(NUM_AUDIO_BUFFER_SAMPLES); ++i)
        waves[0][i] = waves[1][i] = 0.0f;
}
#pragma endregion

#pragma region Message Handlers
void milk2_ui_element::OnActivated()
{
    if (::GetFocus() == get_wnd())
        RegisterFocusHotkeys();

    if (s_fullscreen)
        ApplyFullscreenWindowOrder();
}

void milk2_ui_element::OnDeactivated()
{
    UnregisterFocusHotkeys();
}

void milk2_ui_element::OnSuspending()
{
}

void milk2_ui_element::OnResuming()
{
#ifdef TIMER_DX
    m_timer.ResetElapsedTime();
#endif
}
#pragma endregion

// Properties
void milk2_ui_element::GetDefaultSize(int& width, int& height) const noexcept
{
    width = 640;
    height = 480;
}

void milk2_ui_element::ApplyFrameRateLimit() noexcept
{
    s_config.refresh_frame_rate();
    uint32_t maxFps = normalize_frame_limit(s_config.settings.m_max_fps_fs);
    if (s_config.settings.m_max_fps_fs != maxFps)
        s_config.settings.m_max_fps_fs = maxFps;

    m_refresh_interval = get_refresh_interval_ms(maxFps);
#ifdef TIMER_TP
    LONGLONG frameInterval = 0;
    if (m_frameTimerFrequencyQpc > 0)
    {
        const LONGLONG fps = static_cast<LONGLONG>(maxFps);
        frameInterval = (std::max)(1LL, (m_frameTimerFrequencyQpc + fps / 2) / fps);
    }
    m_frameIntervalQpc = frameInterval;
    m_nextFrameQpc = 0;
    SetFrameTimerResolution(m_milk2 && frameInterval > 0);
#endif

    // The foobar UI scheduler owns frame pacing. Keep the legacy shell sleep
    // limiter disabled so it cannot double-throttle fullscreen frames.
    g_plugin.SetFoobarFullscreenFrameLimit(0);
}

#ifdef TIMER_32
void milk2_ui_element::RestartRefreshTimer() noexcept
{
    m_render_due_from_timer = false;
    KillTimer(ID_REFRESH_TIMER);
    m_last_refresh = GetTickCount64();
    SetTimer(ID_REFRESH_TIMER, m_refresh_interval, nullptr);
}
#endif

void milk2_ui_element::ResizePluginToCurrentClient(const char* context, bool wait_for_lock) noexcept
{
    RECT rect{};
    if (!::GetClientRect(get_wnd(), &rect))
        return;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return;

    normalize_render_size(width, height);
    run_plugin_locked(context, [&] {
        g_plugin.OnWindowMoved();
        g_plugin.OnWindowSizeChanged(width, height);
    }, wait_for_lock);
}

void milk2_ui_element::SetPwd(std::wstring pwd) noexcept
{
    m_pwd.assign(pwd);
}

void milk2_ui_element::ToggleFullScreen()
{
    MILK2_CONSOLE_LOG("ToggleFullScreen0 ", GetWnd())
    if (s_popout)
    {
        TogglePopoutFullscreen();
        return;
    }

    if (m_milk2)
    {
        try
        {
            const bool enteringFullscreen = !s_fullscreen;
            if (enteringFullscreen)
                g_hWindow = get_wnd();
            m_milk2 = false;
#ifdef TIMER_TP
            m_renderPending = false;
            m_renderPostTick = 0;
            StopTimer();
#endif
#if 0
            if (s_fullscreen)
            {
                SetWindowLongPtr(GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowLongPtr(GWL_EXSTYLE, 0);

                int width = 800;
                int height = 600;

                ShowWindow(SW_SHOWNORMAL);

                SetWindowPos(HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            else
            {
                SetWindowLongPtr(GWL_STYLE, WS_POPUP);
                SetWindowLongPtr(GWL_EXSTYLE, WS_EX_TOPMOST);

                SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

                ShowWindow(SW_SHOWMAXIMIZED);
            }
#endif
            s_in_toggle = true;
            s_fullscreen = enteringFullscreen;
            ApplyFrameRateLimit();
#ifdef TIMER_32
            RestartRefreshTimer();
#endif
            SetTopMost();
            static_api_ptr_t<ui_element_common_methods_v2>()->toggle_fullscreen(g_get_guid(), core_api::get_main_window());
            ApplyFullscreenWindowOrder();
            MILK2_CONSOLE_LOG("ToggleFullScreen1 ", GetWnd())
        }
        catch (const std::exception& exc)
        {
            log_runtime_exception("ToggleFullScreen", exc);
        }
        catch (...)
        {
            log_runtime_exception("ToggleFullScreen");
        }
    }
}

void milk2_ui_element::ToggleHelp()
{
    g_plugin.ToggleHelp();
}

void milk2_ui_element::TogglePlaylist()
{
    g_plugin.TogglePlaylist();
}

void milk2_ui_element::ToggleSongTitle()
{
    g_plugin.m_bShowSongTitle = !g_plugin.m_bShowSongTitle;
}

void milk2_ui_element::ToggleSongLength()
{
    if (g_plugin.m_bShowSongTime && g_plugin.m_bShowSongLen)
    {
        g_plugin.m_bShowSongTime = false;
        g_plugin.m_bShowSongLen = false;
    }
    else if (g_plugin.m_bShowSongTime && !g_plugin.m_bShowSongLen)
    {
        g_plugin.m_bShowSongLen = true;
    }
    else
    {
        g_plugin.m_bShowSongTime = true;
        g_plugin.m_bShowSongLen = false;
    }
}

void milk2_ui_element::TogglePresetInfo()
{
    s_config.settings.m_bShowPresetInfo = !s_config.settings.m_bShowPresetInfo;
    g_plugin.m_bShowPresetInfo = s_config.settings.m_bShowPresetInfo;
}

void milk2_ui_element::ToggleFps()
{
    s_config.settings.m_bShowFPS = !s_config.settings.m_bShowFPS;
    g_plugin.m_bShowFPS = s_config.settings.m_bShowFPS;
}

void milk2_ui_element::ToggleRating()
{
    s_config.settings.m_bShowRating = !s_config.settings.m_bShowRating;
    g_plugin.m_bShowRating = s_config.settings.m_bShowRating;
}

void milk2_ui_element::ToggleShaderHelp()
{
    s_config.settings.m_bShowShaderHelp = !s_config.settings.m_bShowShaderHelp;
    g_plugin.m_bShowShaderHelp = s_config.settings.m_bShowShaderHelp;
}

const char* milk2_ui_element::ToggleShuffle(bool forward = true)
{
    auto api = playlist_manager::get();
    size_t nModeCount = api->playback_order_get_count();
    size_t nCurrentMode = api->playback_order_get_active();
    size_t nNewMode = (forward ? ++nCurrentMode % nModeCount : (nCurrentMode == 0 ? nModeCount - 1 : --nCurrentMode));
    api->playback_order_set_active(nNewMode);
    return api->playback_order_get_name(nNewMode);
}

void milk2_ui_element::NextPreset(float fBlendTime)
{
    run_plugin_locked("NextPreset", [&] { g_plugin.NextPreset(fBlendTime); });
}

void milk2_ui_element::PrevPreset(float fBlendTime)
{
    run_plugin_locked("PrevPreset", [&] { g_plugin.PrevPreset(fBlendTime); });
}

bool milk2_ui_element::LoadPreset(int select)
{
#if 0
    g_plugin.m_nCurrentPreset = select + g_plugin.m_nDirs;

    wchar_t szFile[MAX_PATH] = {0};
    wcscpy_s(szFile, g_plugin.m_szPresetDir); // Note: m_szPresetDir always ends with '\'
    wcscat_s(szFile, g_plugin.m_presets[g_plugin.m_nCurrentPreset].szFilename.c_str());

    g_plugin.LoadPreset(szFile, 1.0f);
#else
    UNREFERENCED_PARAMETER(select);
#endif
    return true;
}

std::wstring milk2_ui_element::GetCurrentPreset()
{
    std::wstring currentPreset;
    run_plugin_locked("GetCurrentPreset", [&] {
#ifdef _DEBUG
        wchar_t buf[512]{};
        swprintf_s(buf, L"%s", (g_plugin.m_nLoadingPreset != 0) ? g_plugin.m_pNewState->m_szDesc : g_plugin.m_pState->m_szDesc);
        wcscat_s(buf, L".milk");
        if (g_plugin.m_presets.size() == 0 || g_plugin.m_nCurrentPreset == -1)
        {
            MILK2_CONSOLE_LOG("No presets found")
        }
        else if (wcscmp(buf, g_plugin.m_presets[g_plugin.m_nCurrentPreset].szFilename.c_str()))
        {
            MILK2_CONSOLE_LOG("GetCurrentPreset Mismatch --> ", buf, " != ", g_plugin.m_presets[g_plugin.m_nCurrentPreset].szFilename.c_str())
        }
#endif
        currentPreset = g_plugin.GetCurrentPresetFilename();
    });
    return currentPreset;
}

void milk2_ui_element::OpenCurrentPresetLocation()
{
    std::wstring presetPath;
    run_plugin_locked("OpenCurrentPresetLocation", [&] { presetPath = g_plugin.GetCurrentPresetPath(); });
    if (presetPath.empty())
        return;

    std::wstring arguments = L"/select,\"" + presetPath + L"\"";
    ShellExecute(m_hWnd, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void milk2_ui_element::BlacklistCurrentPreset()
{
    std::wstring presetName;
    run_plugin_locked("BlacklistCurrentPreset current preset", [&] { presetName = g_plugin.GetCurrentPresetFilename(); });
    if (presetName.empty())
        return;

    if (!g_plugin.AddPresetToBlacklist(presetName))
        return;

    KillTimer(ID_BLACKLIST_TIMER);
    m_blacklist_load_retries = 0;
    SetTimer(ID_BLACKLIST_TIMER, 1, nullptr);
}

void milk2_ui_element::LockPreset(bool lockUnlock)
{
    run_plugin_locked("LockPreset", [&] { g_plugin.m_bPresetLockedByUser = lockUnlock; });
}

bool milk2_ui_element::IsPresetLock()
{
    bool locked = false;
    run_plugin_locked("IsPresetLock", [&] { locked = g_plugin.m_bPresetLockedByUser || g_plugin.m_bPresetLockedByCode; });
    return locked;
}

void milk2_ui_element::RandomPreset(float fBlendTime)
{
    run_plugin_locked("RandomPreset", [&] { g_plugin.LoadRandomPreset(fBlendTime); });
}

void milk2_ui_element::SetPresetRating(float inc_dec)
{
    run_plugin_locked("SetPresetRating", [&] { g_plugin.SetCurrentPresetRating(g_plugin.m_pState->m_fRating + inc_dec); });
}

void milk2_ui_element::Seek(UINT nRepCnt, bool bShiftHeldDown, double seekDelta)
{
    int reps = (bShiftHeldDown) ? 6 * nRepCnt : 1 * nRepCnt;
    if (seekDelta > 0.0)
    {
        if (!m_playback_control->playback_can_seek())
        {
            m_playback_control->next();
            return;
        }
    }
    else
    {
        if (!m_playback_control->playback_can_seek())
        {
            m_playback_control->previous();
            return;
        }
        double p = m_playback_control->playback_get_position();
        if (p < 0.0)
            return;
        if (p < (seekDelta * -1.0))
        {
            if (p < (seekDelta * -1.0) / 3.0)
            {
                if (m_playback_control->is_playing() && p > (seekDelta * -1.0))
                    m_playback_control->playback_seek(0.0);
                else
                    m_playback_control->previous();
                return;
            }
            else
            {
                m_playback_control->playback_seek(0.0);
                return;
            }
        }
    }
    for (int i = 0; i < reps; ++i)
        m_playback_control->playback_seek_delta(seekDelta);
}

// clang-format off
void milk2_ui_element::UpdateChannelMode()
{
    if (m_vis_stream.is_valid())
    {
        m_vis_stream->set_channel_mode(s_config.settings.m_bEnableDownmix ? visualisation_stream_v3::channel_mode_mono : visualisation_stream_v3::channel_mode_default);
    }
}

void milk2_ui_element::TogglePopout()
{
    if (s_popout)
        ReturnPopoutToPanel();
    else
        OpenPopoutWindow();
}

void milk2_ui_element::OpenPopoutWindow()
{
    if (!m_milk2 || s_popout || s_fullscreen || !::IsWindow(get_wnd()))
        return;

    HWND hwnd = get_wnd();
    HWND parent = ::GetParent(hwnd);
    if (!parent || !::IsWindow(parent))
        return;

    RECT windowRect{};
    RECT parentRect{};
    if (!::GetWindowRect(hwnd, &windowRect))
        return;

    parentRect = windowRect;
    ::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&parentRect), 2);

    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    width = (std::max)(width, 800);
    height = (std::max)(height, 600);

    HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!::GetMonitorInfo(monitor, &monitorInfo))
        monitorInfo.rcWork = {100, 100, 100 + width, 100 + height};

    const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    width = (std::min)(width, workWidth);
    height = (std::min)(height, workHeight);
    const int x = monitorInfo.rcWork.left + (workWidth - width) / 2;
    const int y = monitorInfo.rcWork.top + (workHeight - height) / 2;

    s_popout_parent = parent;
    s_popout_panel_style = ::GetWindowLongPtr(hwnd, GWL_STYLE);
    s_popout_panel_exstyle = ::GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    s_popout_panel_rect = parentRect;
    s_popout_window_rect = {x, y, x + width, y + height};
    s_popout = true;
    s_popout_fullscreen = false;

#ifdef TIMER_TP
    StopTimer();
#endif

    const DWORD baseStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    const DWORD style = s_config.settings.m_bPopoutBorderless ? (WS_POPUP | baseStyle) : (WS_OVERLAPPEDWINDOW | baseStyle);
    const DWORD exstyle = WS_EX_APPWINDOW;

    ::SetParent(hwnd, nullptr);
    ::SetWindowLongPtr(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));
    ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(exstyle));
    ::SetWindowText(hwnd, L"MilkDrop");
    ::SetWindowPos(hwnd, HWND_TOP, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);

    run_plugin_locked("OpenPopoutWindow mode", [&] { g_plugin.SetScreenMode(WINDOWED); }, true);
    ApplyFrameRateLimit();
    ResizePluginToCurrentClient("OpenPopoutWindow resize", true);

#ifdef TIMER_TP
    StartTimer();
    QueueRenderMessage();
#elif defined(TIMER_32)
    RestartRefreshTimer();
#endif
    InvalidateRect(nullptr, FALSE);
}

void milk2_ui_element::ReturnPopoutToPanel()
{
    if (!s_popout || !::IsWindow(get_wnd()))
        return;

    HWND hwnd = get_wnd();
    if (!s_popout_fullscreen)
        ::GetWindowRect(hwnd, &s_popout_window_rect);

    HWND parent = s_popout_parent;
    if (!parent || !::IsWindow(parent))
    {
        s_popout = false;
        s_popout_fullscreen = false;
        s_popout_parent = nullptr;
        return;
    }

#ifdef TIMER_TP
    StopTimer();
#endif

    s_popout = false;
    s_popout_fullscreen = false;
    ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::SetWindowLongPtr(hwnd, GWL_STYLE, s_popout_panel_style);
    ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, s_popout_panel_exstyle);
    ::SetParent(hwnd, parent);
    ::SetWindowText(hwnd, SHORTNAME);

    const int width = (std::max)(1L, s_popout_panel_rect.right - s_popout_panel_rect.left);
    const int height = (std::max)(1L, s_popout_panel_rect.bottom - s_popout_panel_rect.top);
    ::SetWindowPos(hwnd,
                   HWND_TOP,
                   s_popout_panel_rect.left,
                   s_popout_panel_rect.top,
                   width,
                   height,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    s_popout_parent = nullptr;

    run_plugin_locked("ReturnPopoutToPanel mode", [&] { g_plugin.SetScreenMode(WINDOWED); }, true);
    ApplyFrameRateLimit();
    ResizePluginToCurrentClient("ReturnPopoutToPanel resize", true);

#ifdef TIMER_TP
    StartTimer();
    QueueRenderMessage();
#elif defined(TIMER_32)
    RestartRefreshTimer();
#endif
    InvalidateRect(nullptr, FALSE);
}

void milk2_ui_element::TogglePopoutFullscreen()
{
    if (!s_popout || !m_milk2 || !::IsWindow(get_wnd()))
        return;

    HWND hwnd = get_wnd();

#ifdef TIMER_TP
    StopTimer();
#endif

    const DWORD baseStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    if (!s_popout_fullscreen)
    {
        ::GetWindowRect(hwnd, &s_popout_window_rect);
        HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!::GetMonitorInfo(monitor, &monitorInfo))
            monitorInfo.rcMonitor = s_popout_window_rect;

        s_popout_fullscreen = true;
        ::SetWindowLongPtr(hwnd, GWL_STYLE, static_cast<LONG_PTR>(WS_POPUP | baseStyle));
        ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(WS_EX_APPWINDOW));
        ::SetWindowPos(hwnd,
                       HWND_TOPMOST,
                       monitorInfo.rcMonitor.left,
                       monitorInfo.rcMonitor.top,
                       monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                       monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                       SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        run_plugin_locked("TogglePopoutFullscreen enter", [&] { g_plugin.SetScreenMode(FULLSCREEN); }, true);
    }
    else
    {
        s_popout_fullscreen = false;
        const DWORD style = s_config.settings.m_bPopoutBorderless ? (WS_POPUP | baseStyle) : (WS_OVERLAPPEDWINDOW | baseStyle);
        ::SetWindowLongPtr(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));
        ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(WS_EX_APPWINDOW));
        ::SetWindowPos(hwnd,
                       HWND_NOTOPMOST,
                       s_popout_window_rect.left,
                       s_popout_window_rect.top,
                       s_popout_window_rect.right - s_popout_window_rect.left,
                       s_popout_window_rect.bottom - s_popout_window_rect.top,
                       SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        run_plugin_locked("TogglePopoutFullscreen leave", [&] { g_plugin.SetScreenMode(WINDOWED); }, true);
    }

    ApplyFrameRateLimit();
    ResizePluginToCurrentClient("TogglePopoutFullscreen resize", true);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);

#ifdef TIMER_TP
    StartTimer();
    QueueRenderMessage();
#elif defined(TIMER_32)
    RestartRefreshTimer();
#endif
    InvalidateRect(nullptr, FALSE);
}
// clang-format on

void milk2_ui_element::UpdateFoobarPlaybackState()
{
    const bool active = m_playback_control->is_playing() && !m_playback_control->is_paused();
    run_plugin_locked("UpdateFoobarPlaybackState", [&] { g_plugin.SetFoobarPlaybackActive(active); }, true);
}

void milk2_ui_element::on_playback_starting(play_control::t_track_command /*p_command*/, bool /*p_paused*/)
{
    MILK2_CONSOLE_LOG("+ PlaybackStart")
    UpdateFoobarPlaybackState();
    UpdateTrack();
}

void milk2_ui_element::on_playback_new_track(metadb_handle_ptr p_track)
{
    MILK2_CONSOLE_LOG("+ PlaybackNew")
    UpdateFoobarPlaybackState();
    UpdateTrack(p_track);
}

void milk2_ui_element::on_playback_stop(play_control::t_stop_reason /*p_reason*/)
{
    MILK2_CONSOLE_LOG("+ PlaybackStop")
    UpdateFoobarPlaybackState();
    UpdateTrack();
}

void milk2_ui_element::on_playback_pause(bool /*p_state*/)
{
    MILK2_CONSOLE_LOG("+ PlaybackPause")
    UpdateFoobarPlaybackState();
    UpdateTrack();
}

void milk2_ui_element::UpdateTrack()
{
    if (m_script.is_empty())
    {
        pfc::string8 pattern = pfc::utf8FromWide(s_config.settings.m_szTitleFormat);
        static_api_ptr_t<titleformat_compiler>()->compile_safe_ex(m_script, pattern);
    }

    if (m_playback_control->playback_format_title(NULL, m_state, m_script, NULL, playback_control::display_level_all))
    {
        // Succeeded already.
    }
    else if (m_playback_control->is_playing())
    {
        // Starting playback but not done opening the first track yet.
        m_state = "Opening...";
    }
    else
    {
        m_state = "Stopped.";
    }
}

void milk2_ui_element::UpdateTrack(metadb_handle_ptr p_track)
{
    UpdateTrack();

    if (!p_track.is_valid())
        return;

    // Load the album art.
    if (wcsnlen_s(s_config.settings.m_szArtworkFormat, 256) != 0)
    {
        m_art_file.clear();
        std::vector<uint8_t> empty;
        m_raster.swap(empty);

        titleformat_object::ptr script;
        pfc::string8 pattern = pfc::utf8FromWide(s_config.settings.m_szArtworkFormat);
        bool success = titleformat_compiler::get()->compile(script, pattern);

        pfc::string result;
        if (success && script.is_valid() && p_track->format_title(nullptr, result, script, nullptr))
        {
            if (!result.is_empty())
            {
                m_art_file = pfc::wideFromUTF8(result).c_str();
            }
        }

        ShowAlbumArt();
    }
    else
    {
        LoadAlbumArt(p_track, fb2k::noAbort);
        ShowAlbumArt();
    }
}

void milk2_ui_element::ShowAlbumArt(bool wait_for_lock)
{
    const bool showAlbum = s_config.settings.m_bShowAlbum;
    const std::wstring artFile = m_art_file;
    const std::vector<uint8_t> raster = m_raster;

    if (showAlbum && !artFile.empty())
    {
        pfc::string8 artFileUtf8 = pfc::utf8FromWide(artFile.c_str());
        if (!filesystem::g_exists(artFileUtf8, fb2k::noAbort))
            return;
    }

    run_plugin_locked("ShowAlbumArt", [&] {
        // Only remove the album-art sprite; keep preset/user sprites intact.
        for (int x = 0; x < NUM_TEX; x++)
        {
            if (g_plugin.m_texmgr.m_tex[x].nUserData == 100)
                g_plugin.KillSprite(x);
        }

        if (!showAlbum)
            return;

        if (!artFile.empty()) // file
        {
            g_plugin.LaunchSprite(100, -1, artFile);
        }
        else if (raster.size() > 0) // memory
        {
            g_plugin.LaunchSprite(100, -1, L"", raster);
        }
    }, wait_for_lock);
}

void milk2_ui_element::UpdatePlaylist()
{
    auto api = playlist_manager::get();
    size_t total = api->activeplaylist_get_item_count();
    bool found = false;
    for (size_t i = 0; i < total; ++i)
    {
        if (api->activeplaylist_is_item_selected(i))
        {
            g_plugin.m_playlist_pos = i;
            found = true;
            break;
        }
    }
    if (!found)
    {
        const size_t focus = api->activeplaylist_get_focus_item();
        g_plugin.m_playlist_pos = (focus != pfc::infinite_size && focus < total) ? static_cast<LRESULT>(focus) : 0;
    }
    g_plugin.m_playlist_top_idx = -1;
    g_plugin.m_playlist_line_advance_pixels = 0;
}

void milk2_ui_element::QueueStatusText(const wchar_t* text, float duration, float fadeTime, int fontIndex)
{
    if (!text || text[0] == L'\0')
        return;

    std::lock_guard<std::mutex> lock(m_pending_animated_text_mutex);
    m_pending_animated_text_kind = pending_animated_text_kind::status;
    m_pending_animated_status_text = text;
    m_pending_animated_status_duration = duration;
    m_pending_animated_status_fade_time = fadeTime;
    m_pending_animated_status_font = fontIndex;
}

bool milk2_ui_element::ConsumeClickPauseConfirmation(DWORD now) noexcept
{
    if (!m_click_pause_confirmation_pending)
        return false;

    if (now - m_click_pause_confirmation_tick > ID_CLICK_CONFIRM_TIMEOUT_MS)
    {
        m_click_pause_confirmation_pending = false;
        return false;
    }

    m_click_pause_confirmation_required = false;
    m_click_pause_confirmation_pending = false;
    return true;
}

void milk2_ui_element::QueueClickPauseConfirmation(DWORD now)
{
    m_click_pause_confirmation_required = false;
    m_click_pause_confirmation_pending = true;
    m_click_pause_confirmation_tick = now;
    const int clickPromptFont = s_config.settings.m_bSeparateClickPromptFont ? CLICKPROMPT_FONT : SONGTITLE_FONT;

    if (!m_playback_control->is_playing())
    {
        QueueStatusText(L"Click again to play", 2.4f, 0.35f, clickPromptFont);
    }
    else if (m_playback_control->is_paused())
    {
        QueueStatusText(L"Click again to resume", 2.4f, 0.35f, clickPromptFont);
    }
    else
    {
        QueueStatusText(L"Click again to pause", 2.4f, 0.35f, clickPromptFont);
    }
}

void milk2_ui_element::TogglePlaybackFromClick()
{
    m_click_pause_confirmation_required = false;
    m_click_pause_confirmation_pending = false;

    const bool was_playing = m_playback_control->is_playing() && !m_playback_control->is_paused();
    m_playback_control->toggle_pause();
    if (was_playing)
    {
        const int clickPromptFont = s_config.settings.m_bSeparateClickPromptFont ? CLICKPROMPT_FONT : SONGTITLE_FONT;
        QueueStatusText(L"Paused", 1.6f, 0.35f, clickPromptFont);
    }
    else
        QueueSongTitle();
}

void milk2_ui_element::QueueSongTitle()
{
    std::lock_guard<std::mutex> lock(m_pending_animated_text_mutex);
    m_pending_animated_text_kind = pending_animated_text_kind::song_title;
    m_pending_animated_status_text.clear();
}

void milk2_ui_element::FlushPendingAnimatedText()
{
    pending_animated_text_kind kind = pending_animated_text_kind::none;
    std::wstring statusText;
    float duration = 1.6f;
    float fadeTime = 0.35f;
    int fontIndex = SONGTITLE_FONT;

    {
        std::lock_guard<std::mutex> lock(m_pending_animated_text_mutex);
        kind = m_pending_animated_text_kind;
        if (kind == pending_animated_text_kind::none)
            return;

        statusText = m_pending_animated_status_text;
        duration = m_pending_animated_status_duration;
        fadeTime = m_pending_animated_status_fade_time;
        fontIndex = m_pending_animated_status_font;
        m_pending_animated_text_kind = pending_animated_text_kind::none;
        m_pending_animated_status_text.clear();
    }

    if (kind == pending_animated_text_kind::song_title)
        g_plugin.LaunchSongTitleAnim(true);
    else if (!statusText.empty())
        g_plugin.LaunchStatusText(statusText.c_str(), duration, fadeTime, static_cast<eFontIndex>(fontIndex));
}

bool milk2_ui_element::LaunchSongTitle()
{
    return run_plugin_locked("LaunchSongTitle", [] { g_plugin.LaunchSongTitleAnim(true); }, true);
}

bool milk2_ui_element::LaunchStatusText(const wchar_t* text, float duration, float fadeTime, int fontIndex)
{
    return run_plugin_locked("LaunchStatusText", [&] { g_plugin.LaunchStatusText(text, duration, fadeTime, static_cast<eFontIndex>(fontIndex)); }, true);
}

#ifdef TIMER_TP
// Starts the timer.
void milk2_ui_element::StartTimer() noexcept
{
    MILK2_CONSOLE_LOG_LIMIT("StartTimer ", GetWnd())
    if (!m_milk2)
        return;

    SetFrameTimerResolution(true);
    if (m_framePacerStopEvent == nullptr)
        m_framePacerStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_framePacerWakeEvent == nullptr)
        m_framePacerWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_framePacerStopEvent == nullptr || m_framePacerWakeEvent == nullptr)
        return;

    ResetEvent(m_framePacerStopEvent);
    if (m_framePacerThread == nullptr)
        m_framePacerThread = CreateThread(nullptr, 0, FramePacerThreadProc, this, 0, nullptr);

    ScheduleNextFrameTimer();
}

// Stops the timer.
void milk2_ui_element::StopTimer() noexcept
{
    MILK2_CONSOLE_LOG_LIMIT("StopTimer ", GetWnd())
    if (m_framePacerStopEvent != nullptr)
        SetEvent(m_framePacerStopEvent);
    if (m_framePacerWakeEvent != nullptr)
        SetEvent(m_framePacerWakeEvent);

    if (m_framePacerThread != nullptr)
    {
        WaitForSingleObject(m_framePacerThread, INFINITE);
        CloseHandle(m_framePacerThread);
        m_framePacerThread = nullptr;
    }
    if (m_framePacerWakeEvent != nullptr)
    {
        CloseHandle(m_framePacerWakeEvent);
        m_framePacerWakeEvent = nullptr;
    }
    if (m_framePacerStopEvent != nullptr)
    {
        CloseHandle(m_framePacerStopEvent);
        m_framePacerStopEvent = nullptr;
    }
    m_nextFrameQpc = 0;
    m_renderPending = false;
    m_renderPostTick = 0;
    SetFrameTimerResolution(false);
}

void milk2_ui_element::ScheduleNextFrameTimer() noexcept
{
    if (m_framePacerWakeEvent != nullptr)
        SetEvent(m_framePacerWakeEvent);
}

DWORD WINAPI milk2_ui_element::FramePacerThreadProc(LPVOID context) noexcept
{
    auto* element = static_cast<milk2_ui_element*>(context);
    if (element != nullptr)
        element->FramePacerThreadLoop();
    return 0;
}

void milk2_ui_element::FramePacerThreadLoop() noexcept
{
    HANDLE waits[2] = {m_framePacerStopEvent, m_framePacerWakeEvent};
    if (waits[0] == nullptr || waits[1] == nullptr)
        return;

    for (;;)
    {
        if (WaitForSingleObject(waits[0], 0) == WAIT_OBJECT_0)
            return;

        if (!m_milk2 || !::IsWindow(get_wnd()))
        {
            const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 50);
            if (waitResult == WAIT_OBJECT_0)
                return;
            continue;
        }

        const LONGLONG frameInterval = m_frameIntervalQpc.load(std::memory_order_relaxed);
        if (frameInterval > 0 && m_frameTimerFrequencyQpc > 0)
        {
            for (;;)
            {
                LARGE_INTEGER nowValue{};
                if (!QueryPerformanceCounter(&nowValue))
                    break;

                const LONGLONG now = nowValue.QuadPart;
                LONGLONG nextFrame = m_nextFrameQpc.load(std::memory_order_relaxed);
                if (nextFrame <= 0 || nextFrame < now - frameInterval)
                {
                    nextFrame = now + frameInterval;
                    m_nextFrameQpc.store(nextFrame, std::memory_order_relaxed);
                }

                const LONGLONG ticksUntilFrame = nextFrame - now;
                if (ticksUntilFrame <= 0)
                    break;

                const DWORD waitMs = static_cast<DWORD>((ticksUntilFrame * 1000LL) / m_frameTimerFrequencyQpc);
                if (waitMs > 2)
                {
                    const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, waitMs - 1);
                    if (waitResult == WAIT_OBJECT_0)
                        return;
                    if (waitResult == WAIT_OBJECT_0 + 1)
                        break;
                    continue;
                }

                SwitchToThread();
            }
        }
        else
        {
            m_nextFrameQpc = 0;
            const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 1);
            if (waitResult == WAIT_OBJECT_0)
                return;
        }

        if (!QueueRenderMessage())
        {
            const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 1);
            if (waitResult == WAIT_OBJECT_0)
                return;
        }
    }
}

bool milk2_ui_element::TryBeginFrame(LONGLONG& renderQpc) noexcept
{
    renderQpc = 0;
    if (m_frameTimerFrequencyQpc > 0)
    {
        LARGE_INTEGER nowValue{};
        if (QueryPerformanceCounter(&nowValue))
            renderQpc = nowValue.QuadPart;
    }

    const LONGLONG frameInterval = m_frameIntervalQpc.load(std::memory_order_relaxed);
    if (frameInterval <= 0 || m_frameTimerFrequencyQpc <= 0)
    {
        m_nextFrameQpc = 0;
        return true;
    }

    if (renderQpc <= 0)
    {
        m_nextFrameQpc = 0;
        return true;
    }

    LONGLONG nextFrame = m_nextFrameQpc.load(std::memory_order_relaxed);
    if (nextFrame > 0 && renderQpc < nextFrame)
    {
        ScheduleNextFrameTimer();
        return false;
    }

    return true;
}

void milk2_ui_element::FinishFrameTiming(LONGLONG renderQpc) noexcept
{
    if (renderQpc <= 0 && m_frameTimerFrequencyQpc > 0)
    {
        LARGE_INTEGER nowValue{};
        if (QueryPerformanceCounter(&nowValue))
            renderQpc = nowValue.QuadPart;
    }

    const LONGLONG frameInterval = m_frameIntervalQpc.load(std::memory_order_relaxed);
    if (frameInterval <= 0 || m_frameTimerFrequencyQpc <= 0 || renderQpc <= 0)
    {
        m_nextFrameQpc = 0;
        return;
    }

    LONGLONG nextFrame = renderQpc + frameInterval;
    LARGE_INTEGER nowValue{};
    if (QueryPerformanceCounter(&nowValue))
    {
        while (nextFrame <= nowValue.QuadPart)
            nextFrame += frameInterval;
    }
    m_nextFrameQpc.store(nextFrame, std::memory_order_relaxed);
}

bool milk2_ui_element::QueueRenderMessage() noexcept
{
    if (!m_milk2 || !::IsWindow(get_wnd()))
        return false;

    const DWORD now = GetTickCount();
    const DWORD postedAt = m_renderPostTick.load(std::memory_order_relaxed);
    if (m_renderPending && postedAt != 0 && now - postedAt > 250)
    {
        m_renderPending = false;
        m_renderPostTick = 0;
    }

    bool expected = false;
    if (!m_renderPending.compare_exchange_strong(expected, true))
        return false;

    m_renderPostTick = now;
    if (!::PostMessage(get_wnd(), WM_MILK2_RENDER, 0, 0))
    {
        m_renderPending = false;
        m_renderPostTick = 0;
        return false;
    }
    return true;
}

void milk2_ui_element::SetFrameTimerResolution(bool enabled) noexcept
{
    if (enabled == m_timerResolutionActive)
        return;

    if (enabled)
    {
        if (call_winmm_timer_period("timeBeginPeriod"))
            m_timerResolutionActive = true;
    }
    else
    {
        if (m_timerResolutionActive)
            call_winmm_timer_period("timeEndPeriod");
        m_timerResolutionActive = false;
    }
}
#endif

#ifdef TIMER_DX
bool milk2_ui_element::on_idle()
{
    Tick();
    //MILK2_CONSOLE_LOG("on_idle ", GetWnd())
    return true;
}
#endif

// Sets and unsets foobar2000's "Always on Top" setting (if main window is
// `TOPMOST`) so that visualization window becomes `TOPMOST` on entering
// fullscreen.
// Note: Using `SetWindowPos` does not work because foobar2000 appears to
//       reset itself to `TOPMOST` forcefully when this setting is enabled.
//       So unsetting and resetting the program's setting is a workaround.
bool milk2_ui_element::SetTopMost() noexcept
{
    LONG_PTR ptr = ::GetWindowLongPtr(core_api::get_main_window(), GWL_EXSTYLE);
    bool topmost = static_cast<bool>(ptr & WS_EX_TOPMOST);
    if (topmost)
    {
        if (s_fullscreen)
        {
            MILK2_CONSOLE_LOG("SetTopMost unsetting main window ", GetWnd())
            standard_commands::main_always_on_top(); //::SetWindowPos(get_wnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW /*| SWP_NOZORDER | SWP_FRAMECHANGED*/);
            s_was_topmost = true;
            return true;
        }
    }
    else
    {
        if (s_was_topmost)
        {
            MILK2_CONSOLE_LOG("SetTopMost resetting main window ", GetWnd())
            s_was_topmost = false;
            standard_commands::main_always_on_top(); //::SetWindowPos(get_wnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW /*| SWP_NOZORDER | SWP_FRAMECHANGED*/);
            return true;
        }
    }
    return false;
}

void milk2_ui_element::ApplyFullscreenWindowOrder() noexcept
{
    if (!::IsWindow(get_wnd()))
        return;

    ::SetWindowPos(get_wnd(), s_fullscreen ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void milk2_ui_element::ShowPreferencesPage()
{
    ui_control::get()->show_preferences(guid_milk2_preferences);
}

void milk2_ui_element::SetSelectionSingle(size_t idx)
{
    SetSelectionSingle(idx, false, true, true);
}

void milk2_ui_element::SetSelectionSingle(size_t idx, bool toggle, bool focus, bool single_only)
{
    auto api = playlist_manager::get();
    const size_t total = api->activeplaylist_get_item_count();
    if (idx >= total)
        return;

    const size_t idx_focus = api->activeplaylist_get_focus_item();
    //if (idx_focus == pfc::infinite_size)
    //    return;

    bit_array_bittable mask(total);
    mask.set(idx, toggle ? !api->activeplaylist_is_item_selected(idx) : true);

    if (single_only || toggle || !api->activeplaylist_is_item_selected(idx))
    {
        pfc::bit_array_true baT;
        pfc::bit_array_one baO(idx);
        api->activeplaylist_set_selection(single_only ? (pfc::bit_array&)baT : (pfc::bit_array&)baO, mask);
    }
    if (focus && idx_focus != idx)
        api->activeplaylist_set_focus_item(idx);
}

// Resolves PWD, taking care of the case where the path contains non-ASCII
// characters.
void milk2_ui_element::ResolvePwd()
{
    // Get profile directory path through foobar2000 API.
    pfc::string8 full_path = filesystem::g_get_native_path(core_api::get_my_full_path());
    size_t t = full_path.lastIndexOf(L'\\');
    if (t != SIZE_MAX)
        full_path = full_path.subString(0, t + 1);
    size_t path_length = full_path.get_length();
    std::wstring base_path(path_length + 1, L'\0');
    path_length = pfc::stringcvt::convert_utf8_to_wide(const_cast<wchar_t*>(base_path.c_str()), base_path.size(), full_path.get_ptr(), path_length);
    base_path = base_path.erase(base_path.find(L'\0'));

#if 0
    // Get PWD path through Win32 API.
    wchar_t path[MAX_PATH];
    HMODULE hm = NULL;
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)&TEXT(APPLICATION_FILE_NAME),
                          &hm) == 0)
    {
        DWORD ret = GetLastError();
        MILK2_CONSOLE_LOG("GetModuleHandleEx failed, error = %d\n", ret);
    }
    if (GetModuleFileName(hm, path, MAX_PATH) == 0)
    {
        DWORD ret = GetLastError();
        MILK2_CONSOLE_LOG("GetModuleFileName failed, error = %d\n", ret);
    }
    std::wstring paths(path);
    size_t p = paths.rfind(L'\\');
    if (p != std::wstring::npos)
        paths.erase(p + 1);

    // Use Win32 string it mismatches with the foobar2000 string.
    if (paths != base_path)
    {
        s_pwd = paths;
    }
    else
    {
        s_pwd = base_path;
    }
#else
    s_pwd = base_path;
#endif
}

// Retrieves image raster data and clears the file path.
void milk2_ui_element::ExtractRasterData(const uint8_t* data, size_t size) noexcept
{
    m_art_file.clear();
    std::vector<uint8_t> empty;
    m_raster.swap(empty);
    if ((data != nullptr) && (size != 0))
    {
        m_raster.assign(data, data + size);
    }
}

// Registers with the album art notifier.
void milk2_ui_element::RegisterForArtwork()
{
#if 0
    // Register with the album art notification manager.
    auto AlbumArtNotificationManager = now_playing_album_art_notify_manager_v2::tryGet();

    if (AlbumArtNotificationManager.is_valid())
        AlbumArtNotificationManager->add(this);

    // Get the artwork data from the album art.
    if (wcsnlen_s(s_config.settings.m_szArtworkFormat, 256) == 0)
    {
        auto aanm = now_playing_album_art_notify_manager_v2::get();

        if (aanm != nullptr)
        {
            album_art_data_ptr aad = aanm->current();

            if (aad.is_valid())
            {
                ExtractRasterData(static_cast<const uint8_t*>(aad->data()), aad->size());
            }
        }
    }
#endif
}

// Loads embedded album art.
void milk2_ui_element::LoadAlbumArt(const metadb_handle_ptr& track, abort_callback& abort)
{
    static_api_ptr_t<album_art_manager_v2> aam;

    auto extractor = aam->open(pfc::list_single_ref_t(track), pfc::list_single_ref_t(album_art_ids::cover_front), abort);

    try
    {
        auto aad = extractor->query(album_art_ids::cover_front, abort);

        if (aad.is_valid())
        {
            ExtractRasterData(static_cast<const uint8_t*>(aad->data()), aad->size());
        }
    }
    catch (const exception_album_art_not_found&)
    {
        return;
    }
    catch (const exception_aborted&)
    {
        throw;
    }
    catch (...)
    {
        return;
    }
}

// Service factory publishes the class.
static service_factory_single_t<ui_element_milk2> g_ui_element_milk2_factory;
#pragma endregion

#pragma region Initialize/Quit
class milk2_initquit : public initquit
{
  public:
    void on_init()
    {
        MILK2_CONSOLE_LOG("on_init")
        if (core_api::is_quiet_mode_enabled())
            return;
        install_crash_logging();
        //create_first_run();
    }

    void on_quit()
    {
        MILK2_CONSOLE_LOG("on_quit")
        //NSEEL_quit();
        if (core_api::is_quiet_mode_enabled())
            return;
        if (s_crash_filter_installed)
        {
            SetUnhandledExceptionFilter(s_previous_crash_filter);
            s_previous_crash_filter = nullptr;
            s_crash_filter_installed = false;
        }
        //delete_first_run();
    }

    void create_first_run()
    {
        pfc::string8 milkdrop2_path = filesystem::g_get_native_path(core_api::get_profile_path());
        milkdrop2_path.end_with_slash();
        milkdrop2_path.add_string("milkdrop2\\", 10);
        if (!filesystem::g_exists(milkdrop2_path, fb2k::noAbort))
        {
            filesystem::g_create_directory(milkdrop2_path, fb2k::noAbort);
        }
        pfc::string8 presets_path = milkdrop2_path;
        presets_path.add_string("presets\\", 8);
        if (!filesystem::g_exists(presets_path, fb2k::noAbort))
        {
            filesystem::g_create_directory(presets_path, fb2k::noAbort);
        }
        if (filesystem::g_is_empty_directory(presets_path, fb2k::noAbort))
        {
            pfc::string8 preset_file = presets_path;
            preset_file.add_string("!.milk", 6);
            wchar_t szPresetFile[MAX_PATH];
            pfc::stringcvt::convert_utf8_to_wide(szPresetFile, MAX_PATH, preset_file, preset_file.length());
            char data_buffer[] =
                "MILKDROP_PRESET_VERSION=201\r\nPSVERSION=3\r\nPSVERSION_WARP=3\r\nPSVERSION_COMP=3\r\n[preset00]\r\nfRating=1.000\r\n";
            WriteMilkDropFile(szPresetFile, data_buffer, 106);
        }
    }

    void delete_first_run()
    {
        pfc::string8 preset_file = filesystem::g_get_native_path(core_api::get_profile_path());
        preset_file.end_with_slash();
        preset_file.add_string("milkdrop2\\presets\\!.milk", 24);
        if (filesystem::g_exists(preset_file, fb2k::noAbort))
        {
            filesystem::g_remove(preset_file, fb2k::noAbort);
        }
    }

    void WriteMilkDropFile(LPCWSTR szFile, LPCSTR szDataBuffer, CONST SIZE_T nDataSize)
    {
        HANDLE hFile;
        DWORD dwBytesToWrite = (DWORD)strnlen_s(szDataBuffer, nDataSize);
        DWORD dwBytesWritten = 0;
        BOOL bErrorFlag = FALSE;
        hFile = CreateFile(szFile, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            DisplayError(TEXT("CreateFile"));
            wprintf_s(TEXT("Terminal failure: Unable to open file \"%s\" for write.\n"), szFile);
            wchar_t buf[512] = {0}, title[64] = {0};
            swprintf_s(buf, L"Unable to open `%ls` for writing.", szFile);
            MessageBox(NULL, buf, WASABI_API_LNGSTRINGW_BUF(IDS_MILKDROP_ERROR, title, 64), MB_OK | MB_SETFOREGROUND | MB_TOPMOST);
            return;
        }

        wprintf_s(TEXT("Writing %d bytes to %s.\n"), dwBytesToWrite, szFile);

        bErrorFlag = WriteFile(hFile, szDataBuffer, dwBytesToWrite, &dwBytesWritten, NULL);

        if (FALSE == bErrorFlag)
        {
            DisplayError(TEXT("WriteFile"));
            wprintf_s(TEXT("Terminal failure: Unable to write to file.\n"));
        }
        else
        {
            // This is an error because a synchronous write that results in
            // success (`WriteFile()` returns `TRUE`) should write all data as
            // requested. This would not necessarily be the case for
            // asynchronous writes.
            if (dwBytesWritten != dwBytesToWrite)
            {
                wprintf_s(TEXT("Error: dwBytesWritten != dwBytesToWrite.\n"));
            }
            else
            {
                wprintf_s(TEXT("Wrote %d bytes to %s successfully.\n"), dwBytesWritten, szFile);
            }
        }

        CloseHandle(hFile);
    }
};

FB2K_SERVICE_FACTORY(milk2_initquit);
#pragma endregion
} // namespace

void milk2_sync_runtime_config_from_cfg() noexcept
{
    s_config.reset();
}
